#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <mpg123.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace roguely {

class Id {
    static std::atomic_size_t nextId;
    size_t id{};

public:
    Id() : id{nextId++} {}

    std::string to_string() const;
    size_t get() const { return id; }
};

std::string generate_uuid();

template<typename ...Args>
void println(std::format_string<Args...> fmt, Args && ...args) {
    std::cout << std::format(std::move(fmt), std::forward<Args>(args)...) << std::endl;
}

template <typename T>
class GenericMatrix {
    size_t nr{}, nc{};
    std::vector<std::vector<T>> rows;
public:
    GenericMatrix() {}
    GenericMatrix(size_t nrows, size_t ncols, const T & fill = {}) : nr{nrows}, nc{ncols}, rows(nrows, std::vector<T>(ncols, fill)) {}
    T & operator()(size_t r, size_t c) { return rows.at(r).at(c); }
    const T & operator()(size_t r, size_t c) const { return rows.at(r).at(c); }
    size_t size1() const { return nr; }
    size_t size2() const { return nc; }
    void clear() { *this = GenericMatrix<T>(nr, nc); }
};

using Matrix = GenericMatrix<int>;

struct Point {
    bool operator==(const Point & p) const { return p.x == x && p.y == y; }
    bool operator!=(const Point & p) const { return !(*this == p); }

    int x{};
    int y{};
};

struct Size {
    bool operator==(const Size & s) const { return s.width == width && s.height == height; }
    bool operator!=(const Size & s) const { return !(*this == s); }

    int width{};
    int height{};
};

struct Dimension {
    bool operator==(const Dimension & d) const { return d.point == point && d.supplemental_point == supplemental_point && d.size == size; }
    bool operator!=(const Dimension & d) const { return !(*this == d); }

    Point point{};
    Point supplemental_point{}; // this is a hack for our Janky map drawing
                                // optimization
    Size size{};
};

struct Sound {
    std::string name;
    Mix_Chunk * sound;

public:
    void play() { if (sound != nullptr) Mix_PlayChannel(-1, sound, 0); }
};

class Text {
public:
    int load_font(const std::string & path, int ptsize);
    void draw_text(SDL_Renderer * renderer, int x, int y, const std::string & text);
    void draw_text(SDL_Renderer * renderer, int x, int y, const std::string & text, SDL_Color color);
    Size get_text_extents(const std::string & text);

private:
    TTF_Font * font{};
    std::string text{};
    SDL_Texture * text_texture{};
    SDL_Rect text_rect{};

    SDL_Color text_color = {255, 255, 255, 255};
    SDL_Color text_background_color = {0, 0, 0, 255};
};

enum class EntityGroupName { PLAYER, MOBS, ITEMS, OTHER };

std::string entity_group_name_to_string(EntityGroupName group_name);

class Component {
public:
    Component(const std::string & name, const std::string &id = generate_uuid()) : component_name(name), id(id) {}
    virtual ~Component(){}

    std::string get_name() const { return component_name; }
    void set_name(const std::string & name) { component_name = name; }
    auto get_id() const { return id; }

private:
    std::string component_name;

protected:
    std::string id;
};

template <class T>
concept ComponentType = std::is_base_of<Component, T>::value;

class Entity {
public:
    Entity() : Entity(generate_uuid(), "unnamed entity") {}
    Entity(const std::string & name) : Entity(generate_uuid(), name) {}
    Entity(const std::string & id, const std::string & name) : id(id), name(name) {}

    template <ComponentType T>
    std::shared_ptr<T> find_first_component_by_type() const {
        for (auto & c : components)
            if (auto casted = std::dynamic_pointer_cast<T>(c))
                return casted;
        return nullptr;
    }

    template <ComponentType T>
    std::shared_ptr<T> find_first_component_by_name(const std::string & name) const {
        std::vector<std::shared_ptr<T>> matches{};
        for (auto & c : components)
            if (auto casted = std::dynamic_pointer_cast<T>(c); casted && casted->get_name() == name)
                return casted;
        return nullptr;
    }

    template <ComponentType T>
    auto find_components_by_name(const std::string & name) const {
        std::vector<std::shared_ptr<T>> matches;
        for (auto & c : components)
            if (auto casted = std::dynamic_pointer_cast<T>(c); casted && casted->get_name() == name)
                matches.push_back(casted);
        return matches;
    }

    template <ComponentType T>
    auto find_components_by_type() const {
        std::vector<std::shared_ptr<T>> matches;
        for (auto & c : components)
            if (auto casted = std::dynamic_pointer_cast<T>(c))
                matches.push_back(casted);
        return matches;
    }

    template <ComponentType T>
    auto find_components_by_type(std::function<bool(std::shared_ptr<T>)> predicate) const {
        std::vector<std::shared_ptr<T>> matches;
        for (auto & c : components)
            if (auto casted = std::dynamic_pointer_cast<T>(c); casted && predicate(casted))
                matches.push_back(casted);
        return matches;
    }

    auto get_name() const { return name; }
    auto get_id() const { return id; }
    void add_component(const std::shared_ptr<Component> & c) { components.push_back(c); }
    void add_components(const std::vector<std::shared_ptr<Component>> & c) {
        components.insert(components.end(), c.begin(), c.end());
    }

    template <ComponentType T>
    void remove_components(const std::vector<std::shared_ptr<T>> & cvec) {
        for (auto & component : cvec)
            remove_component(component);
    }

    template <ComponentType T>
    void remove_component(const std::shared_ptr<T> & component) {
        auto it = std::find(components.begin(), components.end(), component);
        if (it != components.end()) it = components.erase(it);
    }

    void for_each_component(const std::function<void(const std::shared_ptr<Component> &)> & fc) const {
        for (auto & c : components) fc(c);
    }

    void clear_components() { components.clear(); }
    size_t get_component_count() const { return components.size(); }

private:
    std::string id{};

protected:
    std::string name;
    std::vector<std::shared_ptr<Component>> components;
};

struct EntityGroup {
    std::string name{};
    std::shared_ptr<std::vector<std::shared_ptr<Entity>>> entities{};
};

class EntityManager {
public:
    EntityManager(sol::this_state s) {
        sol::state_view lua(s);
        lua_entities = lua.create_table();
    }

    void add_entity_to_group(const std::string & group_name, std::shared_ptr<Entity> e, sol::this_state s);
    void add_entity_to_group(EntityGroupName group_name, std::shared_ptr<Entity> e, sol::this_state s) {
        add_entity_to_group(entity_group_name_to_string(group_name), e, s);
    }

    std::shared_ptr<EntityGroup> create_entity_group(const std::string & group_name);
    std::shared_ptr<Entity> create_entity_in_group(const std::string & group_name, const std::string & entity_name);
    void remove_entity(const std::string & entity_group_name, const std::string & entity_id);

    std::vector<std::string> get_entity_group_names() const {
        std::vector<std::string> results;
        results.reserve(entity_groups.size());
        for (const auto & eg : entity_groups) { results.push_back(eg->name); }
        return results;
    }
    std::shared_ptr<EntityGroup> get_entity_group(const std::string & group_name) const;
    std::shared_ptr<EntityGroup> get_entity_group(EntityGroupName group_name) const {
        return get_entity_group(entity_group_name_to_string(group_name));
    }
    std::shared_ptr<std::vector<std::shared_ptr<Entity>>> get_entities_in_group(const std::string & group_name) const;
    std::shared_ptr<std::vector<std::shared_ptr<Entity>>> get_entities_in_group(EntityGroupName group_name) const {
        return get_entities_in_group(entity_group_name_to_string(group_name));
    }

    std::string get_entity_id_by_name(const std::string & group_name, const std::string & entity_name) const;
    std::shared_ptr<Entity> get_entity_by_name(const std::string & group_name, const std::string & entity_name) const;
    std::shared_ptr<Entity> get_entity_by_name(EntityGroupName group_name, const std::string & entity_name) const {
        return get_entity_by_name(entity_group_name_to_string(group_name), entity_name);
    }
    std::shared_ptr<Entity> get_entity_by_id(const std::string & group_name, const std::string & entity_id) const;
    std::shared_ptr<Entity> get_entity_by_id(EntityGroupName group_name, const std::string & entity_id) const {
        return get_entity_by_id(entity_group_name_to_string(group_name), entity_id);
    }

    template <typename T>
    auto find_entities_by_component_type(std::string entity_group, std::function<bool(std::shared_ptr<T>)> predicate) const {
        auto group = get_entity_group(entity_group);

        std::vector<std::shared_ptr<Entity>> matches{};
        for (auto & e : *group->entities) {
            auto result = e->find_components_by_type<T>(predicate);
            if (result.size() > 0) { matches.emplace_back(e); }
        }

        return matches;
    }

    std::shared_ptr<std::vector<std::shared_ptr<Entity>>>
    find_entities_in_group(const std::string & entity_group, std::function<bool(std::shared_ptr<Entity>)> predicate) const;

    std::shared_ptr<Entity> find_entity(const std::string & entity_group,
                                        std::function<bool(std::shared_ptr<Entity>)> predicate) const;
    std::shared_ptr<Entity> find_entity(EntityGroupName entity_group,
                                        std::function<bool(std::shared_ptr<Entity>)> predicate) const {
        return find_entity(entity_group_name_to_string(entity_group), predicate);
    }

    sol::table get_lua_entities() const { return lua_entities; }
    sol::table get_lua_entity(const std::string & entity_group, const std::string & entity_name) const;
    void remove_lua_component(const std::string & entity_group, const std::string & entity_name, const std::string & component_name);

    bool lua_entities_for_each(std::function<bool(sol::table)> predicate) const;
    bool lua_is_point_unique(const Point & point) const;
    void lua_for_each_overlapping_point(const std::string & entity_name, int x, int y, sol::function point_callback);
    sol::table get_lua_blocked_points(const std::string & entity_group, int x, int y, const std::string & direction,
                                      sol::this_state s);
    sol::table get_lua_entities_in_viewport(std::function<bool(int x, int y)> predicate, sol::this_state s);

    static sol::table copy_table(const sol::table & original, sol::this_state s) {
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

private:
    std::vector<std::shared_ptr<EntityGroup>> entity_groups;
    sol::table lua_entities{};
};

// For Lua integration we don't need a bunch of custom components. We'll just
// use a simple component that stores everything in a Lua table
class LuaComponent : public Component {
    sol::table properties;
public:
    LuaComponent(const std::string & n, sol::table props, sol::this_state s) : Component(n) {
        sol::state_view lua(s);
        properties = copy_table(props, s);
    }

    sol::table get_properties() const { return properties; }

    template <typename T>
    T get_property(const std::string & name) {
        if (properties[name].valid()) {
            auto value = properties.get<sol::object>(name);
            if (std::is_same<T, sol::object>::value) { return value; }

            return value.as<T>();
        } else {
            throw std::runtime_error("Property does not exist: " + name);
        }
    }

    void set_property(const std::string & name, sol::object value) { properties.set(name, value); }
    void set_properties(sol::table props, sol::this_state) { properties = props; }

    static sol::table copy_table(const sol::table & original, sol::this_state s);
};

class SpriteSheet {
public:
    SpriteSheet(SDL_Renderer * renderer, const std::string & n, const std::string & p, int sw, int sh, int sf);
    ~SpriteSheet();

    void draw_sprite(SDL_Renderer * renderer, int sprite_id, int x, int y) const;
    void draw_sprite(SDL_Renderer * renderer, int sprite_id, int x, int y, int scale_factor) const;
    void draw_sprite_sheet(SDL_Renderer * renderer, int x, int y) const;

    SDL_Texture * get_spritesheet_texture() const { return spritesheet_texture; }

    std::string get_name() const { return name; }
    int get_sprite_width() const { return sprite_width; }
    int get_sprite_height() const { return sprite_height; }
    int get_scale_factor() const { return scale_factor; }

    sol::table get_sprites_as_lua_table(sol::this_state s) const;

    size_t get_size_of_sprites() const { return sprites.size(); }

    void add_blocked_sprite(int sprite_id) { blocked_sprite_ids.insert(sprite_id); }
    void remove_blocked_sprite(int sprite_id) { blocked_sprite_ids.erase(sprite_id); }
    bool is_sprite_blocked(int sprite_id) const { return blocked_sprite_ids.find(sprite_id) != blocked_sprite_ids.end(); }

    void set_highlight_color(int r, int g, int b) { SDL_SetTextureColorMod(spritesheet_texture, r, g, b); }

    void reset_highlight_color() { SDL_SetTextureColorMod(spritesheet_texture, o_red, o_green, o_blue); }

private:
    Uint8 o_red{}, o_green{}, o_blue{};

    std::set<int> blocked_sprite_ids;
    std::string name;
    std::string path;
    int sprite_width{};
    int sprite_height{};
    int scale_factor{};
    std::vector<SDL_Rect> sprites;
    SDL_Texture * spritesheet_texture{};
};

class Map {
public:
    Map() = default;
    Map(const std::string & n, int w, int h, std::shared_ptr<Matrix> m)
        : name(n), width(w), height(h), map(std::move(m)), light_map(std::make_shared<Matrix>(h, w, 0)){}

    void draw_map(SDL_Renderer * renderer, const Dimension & dimensions,
                  const std::shared_ptr<SpriteSheet> & sprite_sheet,
                  const std::function<void(int, int, int, int, int, int, int)> & draw_hook);

    void draw_map(SDL_Renderer * renderer, const Dimension & dimensions, int x, int y, int a,
                  const std::function<void(int, int, int)> & draw_hook);

    void calculate_field_of_view(const Dimension & dimensions);

    auto get_name() const { return name; }
    auto get_width() const { return width; }
    auto get_height() const { return height; }
    auto get_map() const { return map; }
    auto get_light_map() const { return light_map; }

    auto map_to_world(int x, int y, Dimension dimensions, const SpriteSheet & sprite_sheet) const {
        int scale_factor = sprite_sheet.get_scale_factor();
        int sprite_width = sprite_sheet.get_sprite_width();
        int sprite_height = sprite_sheet.get_sprite_height();

        int dx = (x * sprite_width * scale_factor) - (dimensions.point.x * sprite_width * scale_factor);
        int dy = (y * sprite_height * scale_factor) - (dimensions.point.y * sprite_height * scale_factor);

        return Point{dx, dy};
    }

    Point get_random_point(const std::set<int> & off_limit_sprites_ids) const;

    void trigger_redraw() { current_map_segment_dimension = {}; }

    auto is_point_blocked(int x, int y) { return (*map)(y, x) == 0; }

private:
    // This is our jank optimization for preventing us from creating a new
    // SDL_Texture every frame if nothing has changed. This is used in draw_map.
    Dimension current_map_segment_dimension{};
    Dimension current_full_map_dimension{};
    SDL_Texture * current_map_segment_texture{};
    SDL_Texture * current_full_map_texture{};

    std::string name{};
    int width{};
    int height{};
    std::shared_ptr<Matrix> map{};
    std::shared_ptr<Matrix> light_map{};
};

struct MapInfo {
    std::string name{};
    std::shared_ptr<Map> map{};
};

class AStar {
public:
    AStar() {}

    std::vector<std::pair<int, int>> FindPath(const Matrix & grid, int start_row, int start_col, int goal_row, int goal_col) const;

private:
    // Heuristic function for estimating the distance between two points
    int heuristic(int x1, int y1, int x2, int y2) const { return std::abs(x1 - x2) + std::abs(y1 - y2); }

    // Define the possible movements (up, down, left, right)
    inline static const int dx[4] = {-1, 1,  0, 0};
    inline static const int dy[4] = { 0, 0, -1, 1};

    // Define a typedef for the priority queue entry
    using pq_entry = std::pair<int, std::pair<int, int>>;
};

class Engine {
public:
    Engine();

    int game_loop();

private:
    // Internal functions
    int init_sdl(sol::table game_config, sol::this_state s);
    void tear_down_sdl();
    void setup_lua_api(sol::this_state s);
    bool check_game_config(sol::table game_config, sol::this_state s) const;
    sol::function check_if_lua_function_defined(sol::this_state s, const std::string & name) const;
    void play_sound(const std::string & name);

    // Drawing functions
    void draw_text(const std::string & t, int x, int y);
    void draw_text(const std::string & t, int x, int y, int r, int g, int b, int a);
    void draw_sprite(const std::string & spritesheet_name, int sprite_id, int x, int y, int scale_factor);
    void set_draw_color(SDL_Renderer * renderer, int r, int g, int b, int a) const;
    void draw_point(SDL_Renderer * renderer, int x, int y) const;
    void draw_rect(SDL_Renderer * renderer, int x, int y, int w, int h) const;
    void draw_filled_rect(SDL_Renderer * renderer, int x, int y, int w, int h) const;
    void draw_filled_rect_with_color(SDL_Renderer * renderer, int x, int y, int w, int h, int r, int g, int b, int a) const;
    void draw_graphic(SDL_Renderer * renderer, const std::string & path, int window_width, int x, int y, bool centered,
                      int scale_factor) const;

    static std::shared_ptr<Map> generate_map(const std::string & name, int map_width, int map_height);

    Dimension update_player_viewport(const Point & player_position, const Size & current_map, const Size & initial_view_port);

    std::shared_ptr<Map> find_map(const std::string & name) const {
        auto it = std::find_if(maps.begin(), maps.end(), [&name](const auto & map) { return map->get_name() == name; });
        if (it != maps.end()) return *it;
        return nullptr;
    }

    bool is_within_viewport(int x, int y) const {
        return (x >= view_port_x && x <= view_port_width - 1) && (y >= view_port_y && y <= view_port_height - 1);
    }

    int view_port_x{};
    int view_port_y{};
    int view_port_width{};
    int view_port_height{};
    int VIEW_PORT_WIDTH{};
    int VIEW_PORT_HEIGHT{};

    Dimension current_dimension{};
    MapInfo current_map_info{};

    // Quick and dirty cellular automata that I learned about from YouTube. We can do more but currently are just doing the
    // very least to get a playable level.
    static std::shared_ptr<Matrix> init_cellular_automata(int map_width, int map_height);
    static void perform_cellular_automaton(Matrix & map, int map_width, int map_height, int passes);

    SDL_Window * window{};
    SDL_Renderer * renderer{};
    Mix_Music * soundtrack{};

    // FIXME: Need to have ability to load multiple fonts
    std::weak_ptr<Text> default_font{};

    sol::state lua;
    std::unique_ptr<EntityManager> entity_manager{};
    std::vector<std::shared_ptr<Sound>> sounds{};
    std::unordered_map<std::string, std::shared_ptr<SpriteSheet>> sprite_sheets{};
    std::vector<std::shared_ptr<Map>> maps{};
    std::unordered_map<std::string, std::shared_ptr<Text>> texts{};
    std::unordered_map<std::string, sol::function> systems{};
};
} // namespace roguely
