#include "engine.h"

#ifdef NDEBUG
#undef NDEBUG // force assert() to work
#endif
#include <cassert>
#include <filesystem>
#include <limits>
#include <mutex>
#include <queue>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string_view>

#ifdef _MSC_VER
#pragma warning( disable : 4068 ) /* Disable unknown pragma 'mark' warning */
#endif

namespace roguely {

#pragma mark anon_namespace

namespace {
std::mutex gen_mut;
std::mt19937 gen_mt(std::random_device{}());

int generate_random_int(int min, int max) {
    std::uniform_int_distribution<> dis(min, max);
    std::unique_lock l(gen_mut);
    return dis(gen_mt);
}
#define check_sdl_ptr_or_throw(obj, msg) \
    do { \
        if (!obj) { \
            const auto l = std::source_location::current(); \
            throw std::runtime_error(std::format("{}: {} [{}, line: {}]", msg, SDL_GetError(), \
                                                 std::filesystem::path{l.file_name()}.filename().string(), l.line())); \
        } \
    } while (false)

// Leverage RAII to run a functor at scope end
template <typename Func>
struct Defer {
    Func func;
    Defer(Func && f) : func(std::move(f)) {}
    ~Defer() { func(); }
};
} // namespace

#pragma mark detail

namespace detail {
    void Deleter::operator()(Mix_Chunk *p) const { Mix_FreeChunk(p); }
    void Deleter::operator()(Mix_Music *p) const { Mix_FreeMusic(p); }
    void Deleter::operator()(SDL_Surface *p) const { SDL_FreeSurface(p); }
    void Deleter::operator()(SDL_Texture *p) const { SDL_DestroyTexture(p); }
    void Deleter::operator()(SDL_Renderer *p) const { SDL_DestroyRenderer(p); }
    void Deleter::operator()(SDL_Window *p) const { SDL_DestroyWindow(p); }
    void Deleter::operator()(TTF_Font *p) const { TTF_CloseFont(p); }
} // namespace detail

#pragma mark Id

/* static */ std::atomic_size_t Id::nextId{1u};
std::string Id::to_string() const { return std::format("{}", id); }
std::string generate_uuid() { return Id().to_string(); }

#pragma mark Text

int Text::load_font(const std::string & path, int ptsize) {
    font.reset(TTF_OpenFont(path.c_str(), ptsize));

    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to load font: %s\n%s", path.c_str(), TTF_GetError());
        return -1;
    }

    return 0;
}

Size Text::get_text_extents(const std::string & text) {
    Size ret;
    if (!font || TTF_SizeText(font.get(), text.c_str(), &ret.width, &ret.height) != 0)
        ret = Size{}; // clear to 0 on error
    return ret;
}

void Text::draw_text(SDL_Renderer * renderer, int x, int y, const std::string & text) {
    draw_text(renderer, x, y, text, text_color);
}

void Text::draw_text(SDL_Renderer * renderer, int x, int y, const std::string & t, SDL_Color color) {
    if (t.size() <= 0) return;

    if (text != t) {
        text = t;
        UPtr<SDL_Surface> text_surface{TTF_RenderText_Blended(font.get(), t.c_str(), color)};
        check_sdl_ptr_or_throw(text_surface, "Unable to create surface for text");
        text_texture.reset(SDL_CreateTextureFromSurface(renderer, text_surface.get()));
        check_sdl_ptr_or_throw(text_texture, "Unable to create texture for text");
        text_rect = {.x = x, .y = y, .w = text_surface->w, .h = text_surface->h};
    } else {
        text_rect.x = x;
        text_rect.y = y;
    }

    SDL_RenderCopy(renderer, text_texture.get(), nullptr, &text_rect);
}

#pragma mark EntityGroupName

std::string entity_group_name_to_string(EntityGroupName group_name) {
    std::string gn = "unknown";
    switch (group_name) {
    case EntityGroupName::PLAYER: gn = "player"; break;
    case EntityGroupName::MOBS: gn = "mobs"; break;
    case EntityGroupName::ITEMS: gn = "items"; break;
    case EntityGroupName::OTHER: gn = "other"; break;
    }
    return gn;
}

#pragma mark EntityManager

std::shared_ptr<EntityGroup> EntityManager::create_entity_group(const std::string & group_name) {
    auto entityGroup = std::make_shared<EntityGroup>();
    entityGroup->name = group_name;
    entityGroup->entities = std::make_shared<std::vector<std::shared_ptr<Entity>>>();
    entity_groups.push_back(entityGroup);
    return entityGroup;
}

void EntityManager::add_entity_to_group(const std::string & group_name, std::shared_ptr<Entity> e, sol::this_state s) {
    sol::state_view lua(s);
    auto group = get_entity_group(group_name);
    if (group == nullptr) {
        group = std::make_shared<EntityGroup>();
        group->name = group_name;
        group->entities = std::make_unique<std::vector<std::shared_ptr<Entity>>>();
        entity_groups.push_back(group);
    }
    group->entities->emplace_back(e);

    // Create Lua mapping (entity_group->entity->components)
    sol::table lua_entity_table;
    if (!lua_entities[group_name].valid()) {
        lua_entity_table = lua.create_table();
    } else
        lua_entity_table = lua_entities[group_name];

    auto lua_component = e->find_first_component_by_type<LuaComponent>();

    if (lua_component != nullptr) {
        auto full_name = std::format("{}-{}", e->get_name(), e->get_id());
        lua_entity_table.set(full_name,
                             lua.create_table_with("id", e->get_id(), "name", e->get_name(), "full_name", full_name,
                                                   "components", lua_component->get_properties()));
    }

    lua_entities.set(group_name, lua_entity_table);
}

std::shared_ptr<Entity> EntityManager::create_entity_in_group(const std::string & group_name,
                                                              const std::string & entity_name) {
    auto entity = std::make_shared<Entity>(entity_name);
    auto entity_group = get_entity_group(group_name);
    if (entity_group != nullptr) {
        entity_group->entities->emplace_back(entity);
        return entity;
    }
    return nullptr;
}

void EntityManager::remove_entity(const std::string & entity_group_name, const std::string & entity_id) {
    auto entity_group = get_entity_group(entity_group_name);

    if (entity_group != nullptr) {
        auto entity_to_remove =
            std::find_if(entity_group->entities->begin(), entity_group->entities->end(),
                         [&](std::shared_ptr<Entity> e) { return (e->get_id() == entity_id) ? true : false; });

        if (entity_to_remove != entity_group->entities->end()) {
            const std::string full_name = std::format("{}-{}", (*entity_to_remove)->get_name(), (*entity_to_remove)->get_id());

            // println("Removing entity: {}", full_name);

            sol::table entity_group_table = lua_entities[entity_group_name];
            entity_group_table.set(full_name, sol::nil);

            // println("Checking entity is valid: {}", entity_group_table[full_name].valid());

            entity_group->entities->erase(entity_to_remove);
        }
    }
}

std::shared_ptr<EntityGroup> EntityManager::get_entity_group(const std::string & group_name) const {
    const auto it = std::find_if(entity_groups.begin(), entity_groups.end(),
                                 [&](const std::shared_ptr<EntityGroup> & eg) { return eg->name == group_name; });

    if (it == entity_groups.end())
        return nullptr;
    return *it;
}

std::shared_ptr<std::vector<std::shared_ptr<Entity>>>
EntityManager::get_entities_in_group(const std::string & group_name) const {
    auto entity_group = get_entity_group(group_name);

    if (entity_group != nullptr) { return entity_group->entities; }

    return nullptr;
}

std::string EntityManager::get_entity_id_by_name(const std::string & group_name, const std::string & entity_name) const {
    auto entity = EntityManager::get_entity_by_name(group_name, entity_name);
    if (entity != nullptr) { return entity->get_id(); }
    return "";
}

std::shared_ptr<Entity> EntityManager::get_entity_by_name(const std::string & entity_group,
                                                          const std::string & entity_name) const {
    return find_entity(entity_group, [&](const std::shared_ptr<Entity> & e) { return e->get_name() == entity_name; });
}

std::shared_ptr<Entity> EntityManager::get_entity_by_id(const std::string & entity_group,
                                                        const std::string & entity_id) const {
    return find_entity(entity_group, [&](const std::shared_ptr<Entity> & e) { return e->get_id() == entity_id; });
}

std::shared_ptr<std::vector<std::shared_ptr<Entity>>>
EntityManager::find_entities_in_group(const std::string & entity_group, std::function<bool(std::shared_ptr<Entity>)> predicate) const {
    auto entity_group_ptr = get_entity_group(entity_group);
    auto entities = std::make_shared<std::vector<std::shared_ptr<Entity>>>();

    if (entity_group_ptr != nullptr) {
        for (const auto & e : *entity_group_ptr->entities) {
            if (predicate(e)) { entities->emplace_back(e); }
        }

        return entities;
    }

    return nullptr;
}

std::shared_ptr<Entity> EntityManager::find_entity(const std::string & entity_group,
                                                   std::function<bool(std::shared_ptr<Entity>)> predicate) const {
    auto entity_group_ptr = get_entity_group(entity_group);

    if (entity_group_ptr != nullptr) {
        auto entity = std::find_if(entity_group_ptr->entities->begin(), entity_group_ptr->entities->end(),
                                   [&](const std::shared_ptr<Entity> & e) { return predicate(e); });

        if (entity != entity_group_ptr->entities->end()) { return *entity; }
    }

    return nullptr;
}

sol::table EntityManager::get_lua_entity(const std::string & entity_group, const std::string & entity_name) const {
    sol::table entities = lua_entities[entity_group];
    sol::table result = sol::nil;

    if (!entities.valid()) {
        println("get_lua_entity: entities is not valid");
        return result;
    }

    entities.for_each([&](const sol::object & key, const sol::table & value) {
        if (key.is<std::string>()) {
            std::string key_str = key.as<std::string>();
            if (key_str.compare(0, entity_name.size(), entity_name) == 0) {
                result = value;
                return false;
            }
        }
        return true;
    });

    return result;
}

void EntityManager::remove_lua_component(const std::string & entity_group, const std::string & entity_name,
                                         const std::string & component_name) {
    auto entity = get_lua_entity(entity_group, entity_name);
    if (entity.valid()) {
        auto components = entity["components"];
        if (components.valid()) {
            components[component_name] = sol::nil;
            // println("removed component {} from entity {}", component_name, entity_name);
        }
    }
}

bool EntityManager::lua_entities_for_each(std::function<bool(sol::table)> predicate) const {
    bool result = false;

    for (const auto & eg : entity_groups) {
        for (const auto & e : *eg->entities) {
            auto lua_component = e->find_first_component_by_type<LuaComponent>();
            if (lua_component != nullptr) {
                auto lua_components_table = lua_component->get_properties();
                // FIXME: do we stop iterating if result is false?
                if (lua_components_table.valid()) { result = predicate(lua_components_table); }
            }
        }
    }

    return result;
}

bool EntityManager::lua_is_point_unique(const Point & point) const {
    auto result = true;

    for (const auto & eg : entity_groups) {
        for (const auto & e : *eg->entities) {
            auto lua_component = e->find_first_component_by_type<LuaComponent>();

            if (lua_component != nullptr) {
                auto lua_components_table = lua_component->get_properties();

                if (lua_components_table.valid()) {
                    auto position_component = lua_components_table["position_component"];
                    if (position_component.valid()) {
                        int x = position_component["x"];
                        int y = position_component["y"];

                        if (x == point.x && y == point.y) {
                            result = false;
                            break;
                        }
                    }
                }
            }
        }
    }

    return result;
}

void EntityManager::lua_for_each_overlapping_point(const std::string & entity_name, int x, int y,
                                                   sol::function point_callback) {
    for (const auto & eg : entity_groups) {
        for (const auto & e : *eg->entities) {
            auto lua_component = e->find_first_component_by_type<LuaComponent>();

            if (e->get_name() != entity_name && lua_component != nullptr) {
                auto lua_components_table = lua_component->get_properties();

                if (lua_components_table.valid()) {
                    auto position_component = lua_components_table["position_component"];
                    if (position_component.valid()) {
                        int pc_x = position_component["x"];
                        int pc_y = position_component["y"];

                        if (pc_x == x && pc_y == y) {
                            // println("found overlapping point: Player({}, {}) == Entity({}, {})", x, y, pc_x, pc_y);
                            auto point_callback_result = point_callback(
                                std::format("{}-{}", e->get_name(), e->get_id()), e->get_name(), lua_components_table);
                            if (!point_callback_result.valid()) {
                                sol::error err = point_callback_result;
                                println("Lua script error: {}", err.what());
                            }
                        }
                    }
                }
            }
        }
    }
}

sol::table EntityManager::get_lua_blocked_points(const std::string & entity_group, int x, int y,
                                                 const std::string & direction, sol::this_state s) {
    sol::state_view lua(s);
    auto eg = get_entity_group(entity_group);
    sol::table result = lua.create_table();

    for (const auto & e : *eg->entities) {
        auto lua_component = e->find_first_component_by_type<LuaComponent>();

        if (lua_component != nullptr) {
            auto lua_components_table = lua_component->get_properties();

            if (lua_components_table.valid()) {
                auto position_component = lua_components_table["position_component"];
                if (position_component.valid()) {
                    bool is_blocked = false;

                    int entity_x = position_component["x"];
                    int entity_y = position_component["y"];

                    int up_position_y = y - 1;
                    int down_position_y = y + 1;
                    int left_position_x = x - 1;
                    int right_position_x = x + 1;

                    // UP
                    if (direction == "up" && entity_x == x && up_position_y == entity_y) {
                        is_blocked = true;
                    }
                    // DOWN
                    else if (direction == "down" && entity_x == x && entity_y == down_position_y) {
                        is_blocked = true;
                    }
                    // LEFT
                    else if (direction == "left" && entity_x == left_position_x && entity_y == y) {
                        is_blocked = true;
                    }
                    // RIGHT
                    else if (direction == "right" && entity_x == right_position_x && entity_y == y) {
                        is_blocked = true;
                    }

                    if (is_blocked) {
                        // println("found overlapping point: Player({}, {}) == Entity({}, {})", x, y, entity_x, entity_y);

                        result.set("entity_name", e->get_name());
                        result.set("entity_full_name", std::format("{}-{}", e->get_name(), e->get_id()));
                        result.set("entity_position", lua.create_table_with("x", entity_x, "y", entity_y));
                        result.set("direction", direction);
                        break;
                    }
                }
            }
        }
    }

    return result;
}

sol::table EntityManager::get_lua_entities_in_viewport(std::function<bool(int x, int y)> predicate, sol::this_state s) {
    // loop through all of the entity groups and find the entities that are in the viewport
    // return a table of entities that are in the viewport
    sol::state_view lua(s);
    sol::table result = lua.create_table();

    for (const auto & eg : entity_groups) {
        for (const auto & e : *eg->entities) {
            auto lua_component = e->find_first_component_by_type<LuaComponent>();

            if (lua_component != nullptr) {
                auto lua_components_table = lua_component->get_properties();

                if (lua_components_table.valid()) {
                    auto position_component = lua_components_table["position_component"];
                    if (position_component.valid()) {
                        auto x = position_component["x"];
                        auto y = position_component["y"];

                        if (predicate(x, y)) {
                            const auto fn = std::format("{}-{}", e->get_name(), e->get_id());
                            result.set(fn, lua.create_table_with("group_name", eg->name, "name", e->get_name(), "full_name", fn));
                        }
                    }
                }
            }
        }
    }

    return result;
}

#pragma mark LuaComponent

/* static */
sol::table LuaComponent::copy_table(const sol::table & original, sol::this_state s) {
    sol::state_view lua(original.lua_state());
    sol::table copy = lua.create_table();

    original.for_each([&](const sol::object & key, const sol::object & value) {
        if (value.is<sol::table>()) {
            copy[key] = copy_table(value.as<sol::table>(), s);
        } else {
            copy[key] = value;
        }
    });

    return copy;
}

#pragma mark SpriteSheet

SpriteSheet::SpriteSheet(SDL_Renderer * renderer, const std::string & n, const std::string & p, int sw, int sh, int sf) {
    path = p;
    name = n;
    sprite_width = sw;
    sprite_height = sh;

    if (sf <= 0) sf = 1;

    scale_factor = sf;

    // println("loading spritesheet: {}", path);
    // println("sprite width: {} | sprite height: {}", sprite_width, sprite_height);
    // println("scale factor: {}", scale_factor);

    UPtr<SDL_Surface> tileset{IMG_Load(p.c_str())};
    check_sdl_ptr_or_throw(tileset, "Unable to load tileset");
    spritesheet_texture.reset(SDL_CreateTextureFromSurface(renderer, tileset.get()));
    check_sdl_ptr_or_throw(spritesheet_texture, "Unable to create spritesheet_texture");
    int total_sprites_on_sheet = tileset->w / sw * tileset->h / sh;
    // println("total sprites on sheet: {}", total_sprites_on_sheet);

    SDL_GetTextureColorMod(spritesheet_texture.get(), &o_red, &o_green, &o_blue);

    for (int y = 0; y < total_sprites_on_sheet / (sw + sh); ++y) {
        for (int x = 0; x < total_sprites_on_sheet / (sw + sh); ++x) {
            sprites.push_back({x * sw, y * sh, sw, sh});
        }
    }

    sprites.resize(total_sprites_on_sheet, {0, 0, 0, 0});
}

void SpriteSheet::draw_sprite(SDL_Renderer * renderer, int sprite_id, int x, int y) const {
    draw_sprite(renderer, sprite_id, x, y, scale_factor);
}

void SpriteSheet::draw_sprite(SDL_Renderer * renderer, int sprite_id, int x, int y, int scale_factor) const {
    if (sprite_id < 0 || size_t(sprite_id) >= sprites.size()) {
        println("sprite id out of range: {}", sprite_id);
        return;
    }

    int width = sprite_width;
    int height = sprite_height;

    if (scale_factor > 0) {
        width = sprite_width * scale_factor;
        height = sprite_height * scale_factor;
    }

    const SDL_Rect dest{.x = x, .y = y, .w = width, .h = height};
    auto & sprite_rect = sprites[sprite_id];
    SDL_RenderCopy(renderer, spritesheet_texture.get(), &sprite_rect, &dest);
}

void SpriteSheet::draw_sprite_sheet(SDL_Renderer * renderer, int x, int y) const {
    int col = 0;
    int row_height = 0;

    // println("scale_factor: {}", scale_factor);
    assert(sprites.size() <= size_t(std::numeric_limits<int>::max()));
    for (size_t i = 0; i < sprites.size(); ++i) {
        // println("col * sprite_width = {}", col * sprite_width);
        // println("col * (sprite_width * scale_factor) = {}", col * (sprite_width * scale_factor));

        draw_sprite(renderer, int(i), x + (col * (sprite_width * scale_factor)), y + row_height);
        ++col;

        if ((i + 1u) % 16u == 0u) {
            row_height += sprite_height * scale_factor;
            col = 0;
        }
    }
}

sol::table SpriteSheet::get_sprites_as_lua_table(sol::this_state s) const {
    sol::state_view lua(s);
    sol::table sprites_table = lua.create_table();

    size_t i = 0u;
    for (const auto & sprite_rect : sprites) {
        sol::table rect_table = lua.create_table();

        rect_table.set("x", sprite_rect.x);
        rect_table.set("y", sprite_rect.y);
        rect_table.set("w", sprite_rect.w);
        rect_table.set("h", sprite_rect.h);

        sprites_table.set(i++, rect_table);
    }

    return sprites_table;
}

Point SpriteSheet::map_to_world(const int x, const int y, const Dimension & dimensions) const {
    const int scale_factor = get_scale_factor();
    const int sprite_width = get_sprite_width();
    const int sprite_height = get_sprite_height();

    const int dx = (x * sprite_width * scale_factor) - (dimensions.point.x * sprite_width * scale_factor);
    const int dy = (y * sprite_height * scale_factor) - (dimensions.point.y * sprite_height * scale_factor);
    return Point{.x = dx, .y = dy};
}

#pragma mark Map

void Map::draw_map(SDL_Renderer * renderer, const Dimension & dimensions,
                   const std::shared_ptr<SpriteSheet> & sprite_sheet,
                   const std::function<void(int, int, int, int, int, int, int)> & draw_hook) {
    int scale_factor = sprite_sheet->get_scale_factor();
    int sprite_width = sprite_sheet->get_sprite_width();
    int sprite_height = sprite_sheet->get_sprite_height();

    int texture_width = dimensions.size.width * (sprite_width * scale_factor);
    int texture_height = dimensions.size.height * (sprite_height * scale_factor);

    if (current_map_segment_dimension != dimensions) {
        current_map_segment_dimension = dimensions;

        current_map_segment_texture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                                            texture_width, texture_height));
        check_sdl_ptr_or_throw(current_map_segment_texture, "Unable to create current_map_segment_texture");
        SDL_SetRenderTarget(renderer, current_map_segment_texture.get());
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        for (int rows = dimensions.point.y; rows < dimensions.size.height; ++rows) {
            for (int cols = dimensions.point.x; cols < dimensions.size.width; ++cols) {
                int dx = (cols * sprite_width * scale_factor) - (dimensions.point.x * sprite_width * scale_factor);
                int dy = (rows * sprite_height * scale_factor) - (dimensions.point.y * sprite_height * scale_factor);

                int cell_id = (*map)(rows, cols);

                if (draw_hook != nullptr) {
                    // rows, cols = map Y, X
                    // dx, dy = world X, Y
                    auto light_cell = (*light_map)(rows, cols);
                    // println("light cell: {}", light_cell);
                    draw_hook(rows, cols, dx, dy, cell_id, light_cell, scale_factor);
                }
            }
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    const SDL_Rect destination{.x = 0, .y = 0, .w = texture_width, .h = texture_height};
    SDL_RenderCopy(renderer, current_map_segment_texture.get(), NULL, &destination);
}

void Map::draw_map(SDL_Renderer * renderer, const Dimension & dimensions, int dest_x, int dest_y, int a,
                   const std::function<void(int, int, int)> & draw_hook) {
    if (current_full_map_dimension != dimensions) {
        current_full_map_dimension = dimensions;

        current_full_map_texture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height));
        check_sdl_ptr_or_throw(current_full_map_texture, "Unable to create current_full_map_texture");
        SDL_SetTextureBlendMode(current_full_map_texture.get(), SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer, current_full_map_texture.get());
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_SetTextureAlphaMod(current_full_map_texture.get(), a);
        SDL_RenderClear(renderer);

        for (int rows = 0; rows < height; ++rows) {
            for (int cols = 0; cols < width; ++cols) {
                const int cell_id = (*map)(rows, cols);

                if (draw_hook) draw_hook(rows, cols, cell_id);
            }
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    const SDL_Rect destination{.x = dest_x, .y = dest_y, .w = width, .h = height};
    SDL_RenderCopy(renderer, current_full_map_texture.get(), NULL, &destination);
}

void Map::calculate_field_of_view(const Dimension & dimensions) {
    light_map = std::make_shared<Matrix>(height, width, 0);

    // Iterate through all angles in the 360-degree field of view
    for (int angle = 0; angle < 360; angle += 1) {
        float radians = angle * 0.01745329f; // Conversion factor from degrees to radians
        float dx = std::cos(radians);
        float dy = std::sin(radians);

        float newX = dimensions.supplemental_point.x + dx;
        float newY = dimensions.supplemental_point.y + dy;

        // Keep expanding in the current direction until reaching a wall or map boundary
        while (newX >= 0 && newX < width && newY >= 0 && newY < height) {
            // Mark the cell as visible
            (*light_map)((int)newY, (int)newX) = 1;

            // Stop expanding if a wall is encountered
            if ((*map)((int)newY, (int)newX) == 0) break;

            // Move to the next cell in the current direction
            newX += dx;
            newY += dy;
        }
    }
}

Point Map::get_random_point(const std::set<int> & off_limit_sprites_ids) const {
    if (width <= 0 || height <= 0) { throw std::runtime_error("Empty map"); }

    if (off_limit_sprites_ids.empty()) {
        return Point{.x = generate_random_int(0, width - 1), .y = generate_random_int(0, height - 1)};
    }

    size_t maxAttempts = static_cast<size_t>(height) * static_cast<size_t>(width);
    size_t attempts = 0u;

    while (attempts++ < maxAttempts) {
        int row = generate_random_int(0, height - 1);
        int col = generate_random_int(0, width - 1);

        if ( ! off_limit_sprites_ids.contains((*map)(row, col))) {
            // println("Found random point: ({},{}) = {}", row, col, (*map)(row, col));
            return Point{.x = col, .y = row};
        }
    }

    throw std::runtime_error("Unable to find a random point in map");
}

#pragma mark AStar

std::vector<std::pair<int, int>> AStar::FindPath(const Matrix & grid, int start_row, int start_col, int goal_row, int goal_col) const {
    std::vector<std::pair<int, int>> path;

    // Check if the starting position is a zero
    if (grid(start_col, start_row) != 0) return path;

    // Store grid size
    const int grid_rows = static_cast<int>(grid.size1());
    const int grid_cols = static_cast<int>(grid.size2());

    // Create a priority queue to store the open list
    std::priority_queue<pq_entry, std::vector<pq_entry>, std::greater<pq_entry>> open_list;
    // Create a matrix to store the cost of reaching each cell
    Matrix cost(grid_rows, grid_cols, INT_MAX);
    // Create a matrix to store the parent of each cell
    GenericMatrix<std::pair<int, int>> parent(grid_rows, grid_cols, std::pair{0, 0});
    // Initialize the cost of the start cell to 0
    cost(start_row, start_col) = 0;
    // Add the start cell to the open list with a priority of 0
    open_list.emplace(0, std::make_pair(start_row, start_col));

    while (!open_list.empty()) {
        // Get the cell with the lowest cost from the open list
        int x, y;
        std::tie(x, y) = open_list.top().second;
        open_list.pop();

        // Check if we have reached the goal cell
        if (x == goal_col && y == goal_row) {
            // Reconstruct the path from the goal to the start
            while (x != start_row || y != start_col) {
                path.emplace_back(x, y);
                std::tie(x, y) = parent(x, y);
            }
            path.emplace_back(start_row, start_col);
            std::reverse(path.begin(), path.end());
            return path;
        }

        // Explore the neighbors of the current cell
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i];
            const int ny = y + dy[i];

            // Check if the neighbor is within the grid bounds
            if (nx >= 0 && nx < grid_rows && ny >= 0 && ny < grid_cols && grid(nx, ny) == 0) {
                // Calculate the cost of reaching the neighbor
                const int new_cost = cost(x, y) + 1;

                // Check if the new cost is lower than the current cost
                if (new_cost < cost(nx, ny)) {
                    // Update the cost and parent of the neighbor
                    cost(nx, ny) = new_cost;
                    parent(nx, ny) = std::make_pair(x, y);

                    // Calculate the priority of the neighbor (cost + heuristic)
                    // Add the neighbor to the open list
                    open_list.emplace(new_cost + heuristic(nx, ny, goal_col, goal_row), std::make_pair(nx, ny));
                }
            }
        }
    }
    // no path
    path.clear();
    return path;
}

#pragma mark Engine

/* static */ std::atomic_int Engine::instance_ctr{0};

Engine::Engine() {
    if (++instance_ctr != 1) {
        --instance_ctr; // Note: ~Engine d'tor will never run if we throw here, so we must decrement ctr now.
        throw std::runtime_error("Invariant violated: More than one Engine intances exist!");
    }
}

Engine::~Engine() { tear_down(); --instance_ctr; }

void Engine::initialize(sol::table game_config, sol::this_state) {
    auto throw_if_fail = [](bool failed, std::string_view errMsg, const char *(errFunc)(void)) {
        if (failed) throw std::runtime_error(std::format("{}: {}", errMsg, errFunc()));
    };
    throw_if_fail(Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) != 0,
                  "Failed to initialize Mix_OpenAudio", &Mix_GetError);
    Mix_Volume(-1, 3);
    Mix_VolumeMusic(5);

    throw_if_fail(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0,
                  "Failed to initialize SDL", &SDL_GetError);
    throw_if_fail(IMG_Init(IMG_INIT_PNG) < 0,
                  "Failed to initialize SDL2 IMG", &IMG_GetError);
    throw_if_fail(TTF_Init() != 0,
                  "Failed to initialize SDL_ttf", &TTF_GetError);
    throw_if_fail(Mix_Init(MIX_INIT_MP3) == 0,
                  "Failed to initialize SDL2 mixer", &Mix_GetError);

    entity_manager = std::make_unique<EntityManager>(lua.lua_state());

    std::string window_title = game_config["window_title"];
    std::string window_icon_path = game_config["window_icon_path"];
    int window_width = game_config["window_width"];
    int window_height = game_config["window_height"];
    int spritesheet_sprite_width = game_config["spritesheet_sprite_width"];
    int spritesheet_sprite_height = game_config["spritesheet_sprite_height"];
    int spritesheet_sprite_scale_factor = game_config["spritesheet_sprite_scale_factor"];
    VIEW_PORT_WIDTH = window_width / (spritesheet_sprite_width * spritesheet_sprite_scale_factor);
    VIEW_PORT_HEIGHT = window_height / (spritesheet_sprite_height * spritesheet_sprite_scale_factor);
    current_dimension = {.point = {0, 0}, .size = {VIEW_PORT_WIDTH, VIEW_PORT_HEIGHT}};
    game_config["viewport_width"] = VIEW_PORT_WIDTH;
    game_config["viewport_height"] = VIEW_PORT_HEIGHT;
    game_config["keycodes"] =
        lua.create_table_with(1073741906, "up", 1073741905, "down", 1073741904, "left", 1073741903, "right", 119, "w",
                              97, "a", 115, "s", 100, "d", 32, "space");

    window.reset(SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width,
                                  window_height, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    check_sdl_ptr_or_throw(window, "Failed to create SDL window");

    {
        UPtr<SDL_Surface> window_icon_surface{IMG_Load(window_icon_path.c_str())};
        check_sdl_ptr_or_throw(window_icon_surface, "Unable to load icon");
        SDL_SetWindowIcon(window.get(), window_icon_surface.get());
    }

    renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE));
    check_sdl_ptr_or_throw(renderer, "SDL could not create renderer");

    SDL_SetRenderDrawBlendMode(renderer.get(), SDL_BLENDMODE_BLEND);

    // FIXME: Need to create a way for user defined Text objects
    // std::string font_path = game_config["font_path"];
    // text_medium = std::make_unique<Text>();
    // text_medium->load_font(font_path, 32);

    // Load the spritesheet (FIXME: add ability to load more than one spritesheet)
    sprite_sheets.clear();
    sprite_sheets.try_emplace(game_config["spritesheet_name"],
                              std::make_shared<SpriteSheet>(
                                  renderer.get(), game_config["spritesheet_name"], game_config["spritesheet_path"],
                                  game_config["spritesheet_sprite_width"], game_config["spritesheet_sprite_height"],
                                  game_config["spritesheet_sprite_scale_factor"]));

    // Initialize sounds
    sounds.clear();
    if (game_config["sounds"].valid() && game_config["sounds"].get_type() == sol::type::table) {
        sol::table sound_table = game_config["sounds"];

        for (const auto & [key, value] : sound_table) {
            if (key.get_type() == sol::type::string && value.get_type() == sol::type::string) {
                auto sound_name = key.as<std::string>();
                const auto sound_path = value.as<std::string>();

                // check to see if the sound file exists
                if (!std::filesystem::exists(sound_path)) {
                    println("sound file does not exist: {}", sound_path);
                } else {
                    Sound s{.name = std::move(sound_name), .sound{Mix_LoadWAV(sound_path.c_str())}};
                    check_sdl_ptr_or_throw(s.sound, std::format("Unable to load sound \"{}\"", s.name));
                    sounds.push_back(std::make_shared<Sound>(std::move(s)));
                }
            }
        }
    }

    if (game_config["soundtrack_path"].valid() && game_config["soundtrack_path"].get_type() == sol::type::string) {
        std::string soundtrack_path = game_config["soundtrack_path"];

        soundtrack.reset(Mix_LoadMUS(soundtrack_path.c_str()));
        check_sdl_ptr_or_throw(soundtrack, "Unable to load soundtrack");
        Mix_PlayMusic(soundtrack.get(), 1);
    }
}

void Engine::tear_down() {
    soundtrack.reset();
    sounds.clear();
    sprite_sheets.clear();
    maps.clear();
    texts.clear();
    systems.clear();
    entity_manager.reset();
    lua = sol::state{}; // clear lua

    renderer.reset();
    window.reset();

    Mix_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

void Engine::game_loop() {
    tear_down();

    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::debug, sol::lib::string);

    std::string roguely_script = "roguely.lua";
    if (!std::filesystem::exists(roguely_script))
        throw std::runtime_error(std::format("'{}' does not exist", roguely_script));

    auto game_script = lua.safe_script_file(roguely_script);

    if (!game_script.valid()) {
        sol::error err = game_script;
        throw std::runtime_error(std::format("Lua script error: {}", err.what()));
    }

    auto game_config = lua.get<sol::table>("Game");

    if (!game_config.valid() || !check_game_config(game_config, lua.lua_state()))
        throw std::runtime_error("game script does not define the 'Game' configuration table properly.");

    initialize(game_config, lua.lua_state()); // may throw

    setup_lua_api(lua.lua_state());

    if (check_if_lua_function_defined(lua.lua_state(), "_init")) {
        auto init_result = lua["_init"]();
        if (!init_result.valid()) {
            sol::error err = init_result;
            println("Lua script error: {}", err.what());
        }
    }

    SDL_Event e;
    bool quit = false;
    const int fps = 6;
    const int frame_delay = 1000 / fps;

    Uint32 last_update_time = SDL_GetTicks();
    const Uint32 update_interval = 1000;

    Uint32 frame_start;
    int frame_time;

    while (!quit) {
        frame_start = SDL_GetTicks();

        // handle events
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                if (auto it = systems.find("keyboard_input_system"); it != systems.end()) {
                    // FIXME: Fix hard coded entity group and entity name for PLAYER
                    auto keyboard_input_system_result = it->second(
                        e.key.keysym.sym, entity_manager->get_lua_entity("common", "player"),
                        entity_manager->get_lua_entities(),
                        entity_manager->get_lua_entities_in_viewport(
                            [&](int x, int y) { return is_within_viewport(x, y); }, lua.lua_state()));
                    if (!keyboard_input_system_result.valid()) {
                        sol::error err = keyboard_input_system_result;
                        println("Lua script error: {}", err.what());
                    }
                }
            }
        }

        for (auto & [name, func] : systems) {
            if (name != "tick_system" && name != "keyboard_input_system" && name != "render_system") {
                auto system_result = func(entity_manager->get_lua_entity("common", "player"), entity_manager->get_lua_entities(),
                                          entity_manager->get_lua_entities_in_viewport([&](int x, int y) { return is_within_viewport(x, y); },
                                                                                       lua.lua_state()));
                if (!system_result.valid()) {
                    sol::error err = system_result;
                    throw std::runtime_error(std::format("Lua script error: {}", err.what()));
                }
            }
        }

        Uint32 current_time = SDL_GetTicks();
        if (current_time - last_update_time >= update_interval) {
            if (auto it = systems.find("tick_system"); it != systems.end()) {
                auto tick_system_result = it->second(
                    entity_manager->get_lua_entity("common", "player"), entity_manager->get_lua_entities(),
                    entity_manager->get_lua_entities_in_viewport([&](int x, int y) { return is_within_viewport(x, y); },
                                                                 lua.lua_state())
                );
                if (!tick_system_result.valid()) {
                    sol::error err = tick_system_result;
                    println("Lua script error: {}", err.what());
                }
            }
            last_update_time = current_time;
        }

        SDL_RenderClear(renderer.get());

        // Calculate delta time
        Uint32 current_frame_time = SDL_GetTicks();
        float delta_time = (current_frame_time - frame_start) / 1000.0f;

        // Call render
        if (auto it = systems.find("render_system"); it != systems.end()) {
            auto render_system_result = it->second(
                delta_time, entity_manager->get_lua_entity("common", "player"), entity_manager->get_lua_entities(),
                entity_manager->get_lua_entities_in_viewport([&](int x, int y) { return is_within_viewport(x, y); },
                                                             lua.lua_state()));
            if (!render_system_result.valid()) {
                sol::error err = render_system_result;
                println("Lua script error: {}", err.what());
            }
        }

        SDL_RenderPresent(renderer.get());

        // limit frame rate
        frame_time = SDL_GetTicks() - frame_start;
        if (frame_delay > frame_time) { SDL_Delay(frame_delay - frame_time); }
    }
}

bool Engine::check_game_config(sol::table game_config, sol::this_state) const {
    // Checks that some keys we expect in the game_config table exist, are valid, and are of the expected type
    using RequiredType = std::pair<const char *, sol::type>;
    const RequiredType req_types[] = {
        { "window_title", sol::type::string },
        { "window_width", sol::type::number },
        { "window_height", sol::type::number },
        { "window_icon_path", sol::type::string },
        { "font_path", sol::type::string },
        { "spritesheet_name", sol::type::string },
        { "spritesheet_path", sol::type::string },
        { "spritesheet_sprite_width", sol::type::number },
        { "spritesheet_sprite_height", sol::type::number },
        { "spritesheet_sprite_scale_factor", sol::type::number },
        { "sounds", sol::type::table },
    };
    for (const auto & [name, type] : req_types) {
        const auto obj = game_config[name];
        if (!obj.valid() || obj.get_type() != type) return false;
    }
    return true;
}

void Engine::draw_text(const std::string & t, int x, int y) const { draw_text(t, x, y, 255, 255, 255, 255); }

void Engine::draw_text(const std::string & t, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) const {
    if (t.empty()) return;

    if (!default_font.expired()) {
        const SDL_Color text_color{.r = r, .g = g, .b = b, .a = a};
        default_font.lock()->draw_text(renderer.get(), x, y, t, text_color);
    }
}

void Engine::draw_sprite(const std::string & spritesheet_name, int sprite_id, int x, int y, int scale_factor) const {
    if (auto it = sprite_sheets.find(spritesheet_name); it != sprite_sheets.end()) {
        it->second->draw_sprite(renderer.get(), sprite_id, x, y, scale_factor);
    }
}

void Engine::set_draw_color(SDL_Renderer * renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a) const {
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

void Engine::draw_point(SDL_Renderer * renderer, int x, int y) const { SDL_RenderDrawPoint(renderer, x, y); }

void Engine::draw_rect(SDL_Renderer * renderer, int x, int y, int w, int h) const {
    const SDL_Rect r = {x, y, w, h};
    SDL_RenderDrawRect(renderer, &r);
}

void Engine::draw_filled_rect(SDL_Renderer * renderer, int x, int y, int w, int h) const {
    const SDL_Rect r{.x = x, .y = y, .w = w, .h = h};
    SDL_RenderFillRect(renderer, &r);
}

void Engine::draw_filled_rect_with_color(SDL_Renderer * renderer, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) const {
    const SDL_Rect rect{.x = x, .y = y, .w = w, .h = h};
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
}

void Engine::draw_graphic(SDL_Renderer * renderer, const std::string & path, int window_width, int x, int y,
                          bool centered, int scale_factor) const {
    if (!std::filesystem::exists(path)) {
        println("graphic file does not exist: {}", path);
        return;
    }

    auto *graphic = IMG_Load(path.c_str());
    check_sdl_ptr_or_throw(graphic, "Unable to load graphic file");
    Defer d1([&graphic] { SDL_FreeSurface(graphic); graphic = nullptr; });
    auto *graphic_texture = SDL_CreateTextureFromSurface(renderer, graphic);
    check_sdl_ptr_or_throw(graphic_texture, "Unable to create graphic texture");
    Defer d2([&graphic_texture] { SDL_DestroyTexture(graphic_texture); graphic_texture = nullptr; });

    const SDL_Rect src = {.x = 0, .y = 0, .w = graphic->w, .h = graphic->h};
    SDL_Rect dest = {.x = x, .y = y, .w = graphic->w, .h = graphic->h};

    if (scale_factor > 0) {
        if (centered) dest = {((window_width / (2 + (int)scale_factor)) - (graphic->w / 2)), y, graphic->w, graphic->h};

        SDL_RenderSetScale(renderer, (float)scale_factor, (float)scale_factor);
        SDL_RenderCopy(renderer, graphic_texture, &src, &dest);
        SDL_RenderSetScale(renderer, 1, 1);
    } else {
        if (centered) dest = {((window_width / 2) - (graphic->w / 2)), y, graphic->w, graphic->h};

        SDL_RenderCopy(renderer, graphic_texture, &src, &dest);
    }
}

void Engine::play_sound(const std::string & name) const {
    if (name.empty()) return;
    auto sound = std::find_if(sounds.begin(), sounds.end(), [&](const auto & s) { return s->name == name; });
    if (sound != sounds.end()) (*sound)->play();
}

/* static */
std::shared_ptr<Map> Engine::generate_map(const std::string & name, int map_width, int map_height) {
    auto map = init_cellular_automata(map_width, map_height);
    perform_cellular_automaton(*map, map_width, map_height, 10);

    return std::make_shared<Map>(name, map_width, map_height, std::move(map));
}

/* static */
std::shared_ptr<Matrix> Engine::init_cellular_automata(int map_width, int map_height) {
    assert(map_width >= 0 && map_height >= 0);
    auto ret = std::make_shared<Matrix>(map_height, map_width);
    auto & map = *ret;

    for (int r = 0; r < map_height; ++r) {
        for (int c = 0; c < map_width; ++c) {
            const int z = generate_random_int(1, 100);
            if (z > 48) {
                map(r, c) = 1;
            } else
                map(r, c) = 0;
        }
    }

    return ret;
}

/* static */
void Engine::perform_cellular_automaton(Matrix & map, const int map_width, const int map_height, const int passes) {
    const auto get_neighbor_wall_count = [&map, map_width, map_height](const int x, const int y) {
        int wall_count = 0;

        for (int row = y - 1; row <= y + 1; ++row) {
            for (int col = x - 1; col <= x + 1; ++col) {
                if (row >= 1 && col >= 1 && row < map_height - 1 && col < map_width - 1) {
                    if (map(row, col) == 0) ++wall_count;
                } else {
                    ++wall_count;
                }
            }
        }

        return wall_count;
    };

    for (int p = 0; p < passes; ++p) {
        for (int r = 0; r < map_height; ++r) {
            for (int c = 0; c < map_width; ++c) {
                const int neighbor_wall_count = get_neighbor_wall_count(c, r);

                if (neighbor_wall_count > 4) {
                    map(r, c) = 0; // 0 = wall
                } else
                    map(r, c) = 1; // 1 = floor
            }
        }
    }
}

Dimension Engine::update_player_viewport(const Point & player_position, const Size & current_map,
                                         const Size & initial_view_port [[maybe_unused]]) {
    view_port_x = std::clamp(player_position.x - (VIEW_PORT_WIDTH / 2), 0, current_map.width - VIEW_PORT_WIDTH);
    view_port_y = std::clamp(player_position.y - (VIEW_PORT_HEIGHT / 2), 0, current_map.height - VIEW_PORT_HEIGHT);
    view_port_width = view_port_x + VIEW_PORT_WIDTH;
    view_port_height = view_port_y + VIEW_PORT_HEIGHT;

    const Dimension dim{.point = {.x = view_port_x, .y = view_port_y},
                                  .supplemental_point = player_position,
                                  .size = {.width = view_port_width, .height = view_port_height}};
    if (current_map_info.map != nullptr)
        current_map_info.map->calculate_field_of_view(dim);
    return dim;
}

sol::function Engine::check_if_lua_function_defined(sol::this_state s, const std::string & name) const {
    sol::state_view lua(s);
    sol::function lua_func = lua[name];
    if (!lua_func.valid() || lua_func.get_type() != sol::type::function) {
        println("game script does not define the '{}' method.", name);
        return nullptr;
    }

    return lua_func;
}

void Engine::setup_lua_api(sol::this_state s) {
    sol::state_view lua(s);

    lua.set_function("get_sprite_info", [&](std::string sprite_sheet_name, sol::this_state s) {
        if (auto it = sprite_sheets.find(sprite_sheet_name); it != sprite_sheets.end()) {
            it->second->get_sprites_as_lua_table(s);
        }
    });
    lua.set_function("draw_text", [&](const std::string & t, int x, int y) { draw_text(t, x, y); });
    lua.set_function("draw_text_with_color", [&](const std::string & t, int x, int y, int r, int g, int b, int a) {
        draw_text(t, x, y, Uint8(r), Uint8(g), Uint8(b), Uint8(a));
    });
    lua.set_function("draw_sprite", [&](const std::string & spritesheet_name, int sprite_id, int x, int y) {
        draw_sprite(spritesheet_name, sprite_id, x, y, 0);
    });
    lua.set_function("draw_sprite_scaled",
                     [&](const std::string & spritesheet_name, int sprite_id, int x, int y, int scale_factor) {
                         draw_sprite(spritesheet_name, sprite_id, x, y, scale_factor);
                     });
    lua.set_function("draw_sprite_sheet", [&](const std::string & spritesheet_name, int x, int y) {
        auto ss_i = sprite_sheets.find(spritesheet_name);
        if (ss_i != sprite_sheets.end()) { ss_i->second->draw_sprite_sheet(renderer.get(), x, y); }
    });
    lua.set_function("set_draw_color", [&](int r, int g, int b, int a) { set_draw_color(renderer.get(), Uint8(r), Uint8(g), Uint8(b), Uint8(a)); });
    lua.set_function("draw_point", [&](int x, int y) { draw_point(renderer.get(), x, y); });
    lua.set_function("draw_rect", [&](int x, int y, int w, int h) { draw_rect(renderer.get(), x, y, w, h); });
    lua.set_function("draw_filled_rect", [&](int x, int y, int w, int h) { draw_filled_rect(renderer.get(), x, y, w, h); });
    lua.set_function("draw_filled_rect_with_color", [&](int x, int y, int w, int h, int r, int g, int b, int a) {
        draw_filled_rect_with_color(renderer.get(), x, y, w, h, Uint8(r), Uint8(g), Uint8(b), Uint8(a));
    });
    lua.set_function("draw_graphic",
                     [&](const std::string & path, int window_width, int x, int y, bool centered, int scale_factor) {
                         draw_graphic(renderer.get(), path, window_width, x, y, centered, scale_factor);
                     });
    lua.set_function("play_sound", [&](const std::string & name) { play_sound(name); });
    lua.set_function("get_random_number", [&](int min, int max) { return generate_random_int(min, max); });
    lua.set_function("generate_uuid", [&]() { return generate_uuid(); });
    lua.set_function("generate_map", [&](const std::string & name, int map_width, int map_height) {
        auto map = generate_map(name, map_width, map_height);
        current_map_info.name = name;
        current_map_info.map = map;
        maps.push_back(map);
    });
    lua.set_function("get_random_point_on_map", [&](sol::this_state s) {
        sol::state_view lua(s);
        if (current_map_info.map != nullptr) {
            Point point{0, 0};

            do {
                point = current_map_info.map->get_random_point({0});
            } while (!entity_manager->lua_is_point_unique(point));

            return lua.create_table_with("x", point.x, "y", point.y);
        }
        return lua.create_table();
    });
    lua.set_function("set_map", [&](const std::string & name) {
        auto map = find_map(name);
        if (map != nullptr) {
            current_map_info.map = map;
            current_map_info.name = name;
        }
    });
    lua.set_function("draw_visible_map", [&](const std::string & name, const std::string & ss_name, sol::function draw_map_callback) {
        if (current_map_info.name != name) {
            auto map = find_map(name);

            if (map != nullptr) {
                current_map_info.map = map;
                current_map_info.name = name;
            }
        }

        if (current_map_info.name == name) {
            auto ss_it = sprite_sheets.find(ss_name);
            if (ss_it == sprite_sheets.end()) {
                println("Error, could not find sprite sheet '{}'", ss_name);
                return;
            } else if (!ss_it->second) {
                // should never happen
                println("Error, sprite sheet '{}' is null!", ss_name);
                return;
            }
            current_map_info.map->draw_map(
                renderer.get(), current_dimension, ss_it->second,
                [&](int rows, int cols, int dx, int dy, int cell_id, int light_cell, int scale_factor) {
                    auto draw_map_callback_result =
                        draw_map_callback(rows, cols, dx, dy, cell_id, light_cell, scale_factor);
                    if (!draw_map_callback_result.valid()) {
                        sol::error err = draw_map_callback_result;
                        println("Lua script error: {}", err.what());
                    }
                });
        }
    });
    lua.set_function("draw_full_map", [&](const std::string & name, int x, int y, int a, sol::function draw_map_callback) {
        if (current_map_info.name != name) {
            auto map = find_map(name);

            if (map != nullptr) {
                current_map_info.map = map;
                current_map_info.name = name;
            }
        }

        if (current_map_info.name == name) {
            current_map_info.map->draw_map(renderer.get(), current_dimension, x, y, a, [&](int rows, int cols, int cell_id) {
                auto draw_map_callback_result = draw_map_callback(rows, cols, cell_id);
                if (!draw_map_callback_result.valid()) {
                    sol::error err = draw_map_callback_result;
                    println("Lua script error: {}", err.what());
                }
            });
        }
    });
    lua.set_function("add_entity", [&](const std::string & group_name, const std::string & name, sol::table components,
                                       sol::this_state s) {
        auto entity = std::make_shared<Entity>(name);
        auto components_copy = entity_manager->copy_table(components, s);
        auto lua_component = std::make_shared<LuaComponent>("lua component", components_copy, s);
        entity->add_component(lua_component);
        entity_manager->add_entity_to_group(group_name, entity, s);
    });
    lua.set_function("remove_entity", [&](const std::string & entity_group_name, const std::string & entity_id) {
        entity_manager->remove_entity(entity_group_name, entity_id);
    });
    lua.set_function("remove_component", [&](const std::string & entity_group_name, const std::string & entity_name,
                                             const std::string & component_name) {
        entity_manager->remove_lua_component(entity_group_name, entity_name, component_name);
    });
    lua.set_function("get_component_value",
                     [&](const std::string & entity_group_name, const std::string & entity_name,
                         const std::string & component_name, const std::string & key, sol::this_state s) {
                         sol::state_view lua(s);
                         auto entity = entity_manager->get_entity_by_name(entity_group_name, entity_name);
                         if (entity != nullptr) {
                             auto component = entity->find_first_component_by_type<LuaComponent>();
                             if (component != nullptr) {
                                 auto lua_component = component->get_property<sol::table>(component_name);

                                 if (lua_component != sol::nil) { return (sol::object)lua_component[key]; }
                             }
                         }
                         return (sol::object)lua.create_table();
                     });
    lua.set_function("set_component_value", [&](const std::string & entity_group_name, const std::string & entity_name,
                                                const std::string & component_name, const std::string & key,
                                                sol::object value, sol::this_state) {
        auto entity = entity_manager->get_entity_by_name(entity_group_name, entity_name);
        if (entity != nullptr) {
            auto component = entity->find_first_component_by_type<LuaComponent>();
            if (component != nullptr) {
                auto lua_component = component->get_property<sol::table>(component_name);
                if (lua_component != sol::nil) { lua_component.set(key, value); }
            }
        }
    });
    lua.set_function("update_player_viewport", [&](int x, int y, int width, int height) {
        current_dimension = update_player_viewport(
            {x, y}, {current_map_info.map->get_width(), current_map_info.map->get_height()}, {width, height});
    });
    lua.set_function("get_text_extents", [&](const std::string & t, sol::this_state s) {
        sol::state_view lua(s);
        sol::table extents_table = lua.create_table();
        if (!default_font.expired()) {
            auto extents = default_font.lock()->get_text_extents(t);
            extents_table.set("width", extents.width);
            extents_table.set("height", extents.height);
        }
        return extents_table;
    });
    lua.set_function("add_system", [&](const std::string & name, sol::function system_callback) {
        systems.try_emplace(name, system_callback);
    });
    lua.set_function("get_random_key_from_table", [&](sol::table table) {
        std::string ret;
        if (table.valid() && !table.empty()) {
            int ctr = 0, size = 0;
            table.for_each([&](const sol::object &, const sol::object &) { ++size; });
            const int idx = size == 1 ? 0 : generate_random_int(0, size - 1);
            table.for_each([&](const sol::object & key, const sol::object &) {
                if (ctr++ == idx)
                    ret = key.as<std::string>();
            });
        }
        return ret;
    });
    lua.set_function("find_entity_with_name", [&](const std::string & group_name, const std::string & name) {
        return entity_manager->get_lua_entity(group_name, name);
    });
    lua.set_function("get_overlapping_points",
                     [&](const std::string & entity_name, int x, int y, sol::function point_callback) {
                         return entity_manager->lua_for_each_overlapping_point(entity_name, x, y, point_callback);
                     });
    lua.set_function("get_blocked_points", [&](const std::string & entity_group, int x, int y,
                                               const std::string & direction, sol::this_state s) {
        auto blocked_points = entity_manager->get_lua_blocked_points(entity_group, x, y, direction, s);
        if (!blocked_points.empty() && current_map_info.map != nullptr) { current_map_info.map->trigger_redraw(); }
        return blocked_points;
    });
    lua.set_function("is_within_viewport", [&](int x, int y) { return is_within_viewport(x, y); });
    lua.set_function("force_redraw_map", [&]() {
        if (current_map_info.map != nullptr) { current_map_info.map->trigger_redraw(); }
    });
    lua.set_function("add_font", [&](const std::string & name, const std::string & font_path, int font_size) {
        auto text = std::make_shared<Text>();
        text->load_font(font_path, font_size);
        texts.try_emplace(name, text);
        default_font = text;
    });
    lua.set_function("set_font", [&](const std::string & name) {
        auto it = texts.find(name);
        if (it != texts.end()) { default_font = it->second; }
    });
    lua.set_function("get_adjacent_points", [&](int x, int y, sol::this_state s) {
        sol::state_view lua(s);
        std::vector<Point> points = {/* UP    */ {x, y - 1},
                                                      /* DOWN  */ {x, y + 1},
                                                      /* LEFT  */ {x - 1, y},
                                                      /* RIGHT */ {x + 1, y}};

        auto is_up_blocked = entity_manager->lua_is_point_unique(points[0]) &&
                             current_map_info.map->is_point_blocked(points[0].x, points[0].y);
        auto is_down_blocked = entity_manager->lua_is_point_unique(points[1]) &&
                               current_map_info.map->is_point_blocked(points[1].x, points[1].y);
        auto is_left_blocked = entity_manager->lua_is_point_unique(points[2]) &&
                               current_map_info.map->is_point_blocked(points[2].x, points[2].y);
        auto is_right_blocked = entity_manager->lua_is_point_unique(points[3]) &&
                                current_map_info.map->is_point_blocked(points[3].x, points[3].y);
        entity_manager->lua_is_point_unique(points[3]) &&
            current_map_info.map->is_point_blocked(points[3].x, points[3].y);

        sol::table adjacent_points = lua.create_table_with(
            "up", lua.create_table_with("blocked", is_up_blocked, "x", points[0].x, "y", points[0].y), "down",
            lua.create_table_with("blocked", is_down_blocked, "x", points[1].x, "y", points[1].y), "left",
            lua.create_table_with("blocked", is_left_blocked, "x", points[2].x, "y", points[2].y), "right",
            lua.create_table_with("blocked", is_right_blocked, "x", points[3].x, "y", points[3].y));
        return adjacent_points;
    });
    lua.set_function("map_to_world", [&](int x, int y, const std::string & ss_name, sol::this_state s) {
        sol::state_view lua(s);
        sol::table table = lua.create_table();
        if (auto ss_it = sprite_sheets.find(ss_name); ss_it != sprite_sheets.end()) {
            auto point = ss_it->second->map_to_world(x, y, current_dimension);
            table.set("x", point.x);
            table.set("y", point.y);
        }
        return table;
    });
    lua.set_function("set_highlight_color", [&](const std::string & ss_name, int r, int g, int b) {
        if (auto it = sprite_sheets.find(ss_name); it != sprite_sheets.end())
            it->second->set_highlight_color(Uint8(r), Uint8(g), Uint8(b));
    });
    lua.set_function("reset_highlight_color", [&](const std::string & ss_name) {
        if (auto it = sprite_sheets.find(ss_name); it != sprite_sheets.end()) {
            it->second->reset_highlight_color();
            if (current_map_info.map != nullptr) { current_map_info.map->trigger_redraw(); }
        }
    });
}

} // namespace roguely
