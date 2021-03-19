#include "Game.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace roguely::game
{
		Game::Game()
		{
				maps = std::make_shared<std::vector<std::shared_ptr<roguely::common::Map>>>();
				entity_groups = std::make_shared<std::vector<std::shared_ptr<roguely::ecs::EntityGroup>>>();
		}

		void Game::generate_map_for_testing()
		{
				auto map = roguely::level_generation::init_cellular_automata(100, 40);
				roguely::level_generation::perform_cellular_automaton(map, 100, 40, 10);

				for (int row = 0; row < 40; row++)
				{
						for (int column = 0; column < 100; column++)
						{
								if ((*map)(row, column) == 0) {
										std::cout << "#";
								}
								else if ((*map)(row, column) == 9) {
										std::cout << ".";
								}
						}

						std::cout << std::endl;
				}
		}

		void Game::switch_map(std::string name)
		{
				auto map = std::find_if(maps->begin(), maps->end(),
						[&](const std::shared_ptr<roguely::common::Map>& m) {
								return m->name == name;
						});

				if (map != maps->end()) {
						current_map = *map;
				}
		}

		std::shared_ptr<roguely::common::Map> Game::get_map(std::string name)
		{
				auto map = std::find_if(maps->begin(), maps->end(),
						[&](const std::shared_ptr<roguely::common::Map>& m) {
								return m->name == name;
						});

				if (map != maps->end()) {
						return *map;
				}

				return nullptr;
		}

		std::shared_ptr<roguely::ecs::EntityGroup> Game::get_entity_group(std::string name)
		{
				auto group = std::find_if(entity_groups->begin(), entity_groups->end(),
						[&](const std::shared_ptr<roguely::ecs::EntityGroup>& eg) {
								return eg->name == name;
						});

				if (group != entity_groups->end()) {
						return *group;
				}

				return nullptr;
		}

		std::shared_ptr<roguely::ecs::Entity> Game::get_entity(std::shared_ptr<roguely::ecs::EntityGroup> entity_group, std::string entity_id)
		{
				auto entity = std::find_if(entity_group->entities->begin(), entity_group->entities->end(),
						[&](const std::shared_ptr<roguely::ecs::Entity>& m) {
								return m->get_id() == entity_id;
						});

				if (entity != entity_group->entities->end())
				{
						return *entity;
				}

				return nullptr;
		}

		std::shared_ptr<roguely::ecs::EntityGroup> Game::create_entity_group(std::string name)
		{
				auto entityGroup = std::make_shared<roguely::ecs::EntityGroup>();
				entityGroup->name = name;
				entityGroup->entities = std::make_shared<std::vector<std::shared_ptr<roguely::ecs::Entity>>>();
				entity_groups->emplace_back(entityGroup);
				return entityGroup;
		}

		std::shared_ptr<roguely::ecs::Entity> Game::add_entity_to_group(std::shared_ptr<roguely::ecs::EntityGroup> entityGroup, roguely::ecs::EntityType entity_type, std::string id, roguely::common::Point point)
		{
				auto entity = std::make_shared<roguely::ecs::Entity>(entityGroup, id, point, entity_type);

				if (entity_type == roguely::ecs::EntityType::Player)
				{
						player_id = id;
						player = entity;

				}

				entityGroup->entities->emplace_back(entity);

				return entity;
		}

		void Game::add_sprite_component(std::shared_ptr<roguely::ecs::Entity> entity, std::string spritesheet_name, int sprite_in_spritesheet_id, std::string sprite_name)
		{
				auto sprite_component = std::make_shared<roguely::ecs::SpriteComponent>(spritesheet_name, sprite_in_spritesheet_id, sprite_name);
				sprite_component->set_component_name("sprite_component");
				entity->add_component(sprite_component);
		}

		void Game::add_health_component(std::shared_ptr<roguely::ecs::Entity> entity, int h)
		{
				auto health_component = std::make_shared<roguely::ecs::HealthComponent>(h);
				health_component->set_component_name("health_component");
				entity->add_component(health_component);
		}

		void Game::add_stats_component(std::shared_ptr<roguely::ecs::Entity> entity, int a)
		{
				auto stats_component = std::make_shared<roguely::ecs::StatsComponent>(a);
				stats_component->set_component_name("stats_component");
				entity->add_component(stats_component);
		}

		void Game::add_score_component(std::shared_ptr<roguely::ecs::Entity> entity, int s)
		{
				auto score_component = std::make_shared<roguely::ecs::ScoreComponent>(s);
				score_component->set_component_name("score_component");
				entity->add_component(score_component);
		}

		void Game::add_value_component(std::shared_ptr<roguely::ecs::Entity> entity, int v)
		{
				auto value_component = std::make_shared<roguely::ecs::ValueComponent>(v);
				value_component->set_component_name("value_component");
				entity->add_component(value_component);
		}

		void Game::add_inventory_component(std::shared_ptr<roguely::ecs::Entity> entity, std::vector<std::pair<std::string, int>> items)
		{
				auto inventory_component = std::make_shared<roguely::ecs::InventoryComponent>();

				for (auto& item : items)
				{
						inventory_component->add_item(item.first, item.second);
				}

				inventory_component->set_component_name("inventory_component");
				entity->add_component(inventory_component);
		}

		void Game::add_lua_component(std::shared_ptr<roguely::ecs::Entity> entity, std::string n, std::string t, sol::table props)
		{
				auto lua_component = std::make_shared<roguely::ecs::LuaComponent>(n, t, props);
				std::ostringstream lua_component_name;
				lua_component_name << "_component";
				lua_component->set_component_name(lua_component_name.str());
				entity->add_component(lua_component);
		}

		std::string Game::generate_uuid()
		{
				boost::uuids::random_generator gen;
				boost::uuids::uuid id = gen();
				return boost::uuids::to_string(id);
		}

		bool Game::is_tile_player_tile(int x, int y, roguely::common::MovementDirection dir)
		{
				return ((dir == roguely::common::MovementDirection::Up && player->y() == y - 1 && player->x() == x) ||
						(dir == roguely::common::MovementDirection::Down && player->y() == y + 1 && player->x() == x) ||
						(dir == roguely::common::MovementDirection::Left && player->y() == y && player->x() == x - 1) ||
						(dir == roguely::common::MovementDirection::Right && player->y() == y && player->x() == x + 1));
		}

		auto Game::is_entity_location_traversable(int x, int y, std::shared_ptr<std::vector<std::shared_ptr<roguely::ecs::Entity>>> entities, roguely::common::WhoAmI whoAmI, roguely::common::MovementDirection dir)
		{
				for (const auto& e : *entities)
				{
						if ((dir == roguely::common::MovementDirection::Up && e->y() == y - 1 && e->x() == x) ||
								(dir == roguely::common::MovementDirection::Down && e->y() == y + 1 && e->x() == x) ||
								(dir == roguely::common::MovementDirection::Left && e->y() == y && e->x() == x - 1) ||
								(dir == roguely::common::MovementDirection::Right && e->y() == y && e->x() == x + 1))
						{
								if (e->get_entity_type() == roguely::ecs::EntityType::Enemy || whoAmI == roguely::common::WhoAmI::Enemy)
								{
										TileWalkableInfo twi{
												false,
												{ e->x(), e->y() },
												e->get_entity_type()
										};

										return std::make_shared<TileWalkableInfo>(twi);
								}
						}
				}

				// if we get this far its not any of our other entities, most likely open ground
				TileWalkableInfo twi{
						true,
						{ x, y },
						roguely::ecs::EntityType::Ground /* treat everything as ground if its traversable */
				};

				return std::make_shared<TileWalkableInfo>(twi);
		}

		bool Game::is_tile_on_map_traversable(int x, int y, roguely::common::MovementDirection dir, int tileId)
		{
				if (current_map->map == nullptr) return false;

				// TODO: Some checks with fail for instance anything at the edges. We need to account for this!

				return !(dir == roguely::common::MovementDirection::Up && (*current_map->map)((size_t)y - 1, x) == tileId ||
						dir == roguely::common::MovementDirection::Down && (*current_map->map)((size_t)y + 1, x) == tileId ||
						dir == roguely::common::MovementDirection::Left && (*current_map->map)(y, (size_t)x - 1) == tileId ||
						dir == roguely::common::MovementDirection::Right && (*current_map->map)(y, (size_t)x + 1) == tileId);
		}

		TileWalkableInfo Game::is_tile_walkable(int x, int y, roguely::common::MovementDirection dir, roguely::common::WhoAmI whoAmI, std::vector<std::string> entity_groups_to_check)
		{
				bool walkable = false;
				auto is_player = player->x() == x && player->y() == y;

				// for enemy movement
				if (is_tile_player_tile(x, y, dir)) {
						return { false, { x, y }, roguely::ecs::EntityType::Player };
				};

				for (auto& egtc : entity_groups_to_check)
				{
						auto group = std::find_if(entity_groups->begin(), entity_groups->end(),
								[&](const auto& eg) {
										return eg->name == egtc;
								});

						walkable = is_tile_on_map_traversable(x, y, dir, 0 /* Wall */);

						if (!walkable) {
								return { false, { x, y }, roguely::ecs::EntityType::Wall };
						}

						if (group != entity_groups->end())
						{
								auto walkableInfo = is_entity_location_traversable(x, y, (*group)->entities, whoAmI, dir);

								if (walkableInfo->walkable) {
										return *walkableInfo;
								}
						}
				}

				return {};
		}

		bool Game::is_xy_blocked(int x, int y, std::vector<std::string> entity_groups_to_check)
		{
				if (player->x() == x || player->y() == y) return true;
				if (current_map->map == nullptr) return true;
				if ((*current_map->map)(y, x) == 0) return true;

				bool blocked = true;

				for (auto& egtc : entity_groups_to_check)
				{
						auto group = get_entity_group(egtc);
						if (group != nullptr && is_entity_location_traversable(x, y, group->entities)) blocked = false;
						else {
								blocked = true;
								break;
						}
				}

				return blocked;
		}

		roguely::common::Point Game::generate_random_point(std::vector<std::string> entity_groups_to_check)
		{
				if (current_map->map == nullptr) return {};

				int c = 0;
				int r = 0;

				do
				{
						c = std::rand() % (current_map->width - 1);
						r = std::rand() % (current_map->height - 1);
				} while (is_xy_blocked(c, r, entity_groups_to_check));

				return { c, r };
		}

		roguely::common::Point Game::get_open_point_for_xy(int x, int y, std::vector<std::string> entity_groups_to_check)
		{
				int left = x - 1;
				int right = x + 1;
				int up = y - 1;
				int down = y + 1;

				if (!is_xy_blocked(left, y, entity_groups_to_check)) return { left, y };
				else if (!is_xy_blocked(right, y, entity_groups_to_check)) return { right, y };
				else if (!is_xy_blocked(x, up, entity_groups_to_check)) return { x, up };
				else if (!is_xy_blocked(x, down, entity_groups_to_check)) return { x, down };

				return generate_random_point(entity_groups_to_check);
		}

		void Game::update_player_viewport_points()
		{
				//std::cout << "current_map->width = " << current_map->width
				//		<< ", current_map->height = " << current_map->height
				//		<< std::endl;

				//std::cout << "view_port_x = " << view_port_x
				//		<< ", view_port_y = " << view_port_y
				//		<< std::endl;

				//std::cout << "view_port_width = " << view_port_width
				//		<< ", view_port_height = " << view_port_height
				//		<< std::endl;

				//std::cout << "player is (" << player->x() << ", " << player->y() << ")" << std::endl;

				view_port_x = player->x() - VIEW_PORT_WIDTH;
				view_port_y = player->y() - VIEW_PORT_HEIGHT;

				if (view_port_x < 0) view_port_x = std::max(0, view_port_x);
				if (view_port_x > (current_map->width - (VIEW_PORT_WIDTH * 2))) view_port_x = (current_map->width - (VIEW_PORT_WIDTH * 2));

				if (view_port_y < 0) view_port_y = std::max(0, view_port_y);
				if (view_port_y > (current_map->height - (VIEW_PORT_HEIGHT * 2))) view_port_y = (current_map->height - (VIEW_PORT_HEIGHT * 2));

				view_port_width = view_port_x + (VIEW_PORT_WIDTH * 2);
				view_port_height = view_port_y + (VIEW_PORT_HEIGHT * 2);
		}

		std::shared_ptr<roguely::ecs::Entity> Game::update_entity_position(std::string entity_group_name, std::string entity_id, int x, int y)
		{
				std::shared_ptr<roguely::ecs::Entity> entity{};

				if (entity_id == "player" || player->get_id() == entity_id)
				{
						player->set_point({ x, y });

						//std::cout << "set player to ("
						//		<< x
						//		<< ", "
						//		<< y
						//		<< ")"
						//		<< std::endl;

						//std::cout << "player is (" 
						//				  << player->x() 
						//					<< ", " 
						//				  << player->y() 
						//				  << ")" 
						//				  << std::endl;

						update_player_viewport_points();
						rb_fov();
						entity = player;
				}
				else
				{
						auto entity_group = get_entity_group(entity_group_name);

						if (entity_group != nullptr)
						{
								entity = get_entity(entity_group, entity_id);

								if (entity != nullptr) {
										entity->set_point({ x, y });
								}
						}
				}

				return entity;
		}

		int Game::get_component_value(std::shared_ptr<roguely::ecs::Component> component, std::string key)
		{
				int result = -1;

				if (component->get_component_name() == "score_component")
				{
						auto sc = std::dynamic_pointer_cast<roguely::ecs::ScoreComponent>(component);
						if (sc != nullptr)
						{
								result = sc->get_score();
						}
				}
				else if (component->get_component_name() == "health_component")
				{
						auto hc = std::static_pointer_cast<roguely::ecs::HealthComponent>(component);
						if (hc != nullptr)
						{
								result = hc->get_health();
						}
				}
				else if (component->get_component_name() == "stats_component")
				{
						auto sc = std::static_pointer_cast<roguely::ecs::StatsComponent>(component);
						if (sc != nullptr)
						{
								result = sc->get_attack();
						}
				}

				return result;
		}

		int Game::get_component_value(std::string entity_group_name, std::string entity_id, std::string component_name, std::string key)
		{
				int result = -1;

				if (entity_id == "player" || player->get_id() == entity_id)
				{
						auto component = player->find_component_by_name(component_name);

						if (component != nullptr)
								result = get_component_value(component, key);
						else
						{
								auto entity_group = get_entity_group(entity_group_name);

								if (entity_group != nullptr)
								{
										auto entity = get_entity(entity_group, entity_id);

										if (entity != nullptr)
										{
												auto component = entity->find_component_by_name(component_name);

												if (component != nullptr)
														result = get_component_value(component, key);
										}
								}
						}
				}

				return result;
		}

		bool Game::set_component_value(std::shared_ptr<roguely::ecs::Component> component, std::string key, int value)
		{
				bool did_update = false;

				if (component->get_component_name() == "score_component") {
						auto sc = std::dynamic_pointer_cast<roguely::ecs::ScoreComponent>(component);
						if (sc != nullptr)
						{
								sc->update_score(value);
								did_update = true;
						}
				}
				else if (component->get_component_name() == "health_component") {
						auto hc = std::static_pointer_cast<roguely::ecs::HealthComponent>(component);
						if (hc != nullptr)
						{
								hc->set_health(value);
								did_update = true;
						}
				}
				else if (component->get_component_name() == "stats_component") {
						auto sc = std::static_pointer_cast<roguely::ecs::StatsComponent>(component);
						if (sc != nullptr)
						{
								sc->set_attack(value);
								did_update = true;
						}
				}

				return did_update;
		}

		std::shared_ptr<roguely::ecs::Entity> Game::set_component_value(std::string entity_group_name, std::string entity_id, std::string component_name, std::string key, int value)
		{
				if (entity_id == "player" || player->get_id() == entity_id)
				{
						auto component = player->find_component_by_name(component_name);

						if (component != nullptr)
						{
								auto did_update = set_component_value(component, key, value);

								if (did_update)
										return player;
						}
				}
				else
				{
						auto entity_group = get_entity_group(entity_group_name);

						if (entity_group != nullptr)
						{
								auto entity = get_entity(entity_group, entity_id);

								if (entity != nullptr)
								{
										auto component = entity->find_component_by_name(component_name);

										if (component != nullptr)
										{
												auto did_update = set_component_value(component, key, value);

												if (did_update)
														return player;
										}
								}
						}
				}

				return nullptr;
		}

		std::shared_ptr<roguely::ecs::Entity> Game::set_component_value(std::string entity_group_name, std::string entity_id, std::string component_name, std::string key, std::pair<std::string, int> value)
		{
				if (component_name != "inventory_component") return nullptr;

				if (entity_id == "player" || player->get_id() == entity_id)
				{
						auto component = player->find_component_by_name(component_name);

						if (component != nullptr)
						{
								auto ic = std::dynamic_pointer_cast<std::shared_ptr<roguely::ecs::InventoryComponent>>(component);
								if (ic != nullptr)
								{
										(*ic)->upsert_item(value);
										return player;
								}
						}
				}

				return nullptr;
		}

		// Taken from http://www.roguebasin.com/index.php?title=Eligloscode
		// Modified to fit in my game
		void Game::rb_fov()
		{
				float x = 0, y = 0;

				current_map->light_map = std::make_shared<boost::numeric::ublas::matrix<int>>(current_map->height, current_map->width);

				for (int r = 0; r < current_map->height; r++)
				{
						for (int c = 0; c < current_map->width; c++)
						{
								(*current_map->light_map)(r, c) = 0;
						}
				}

				for (int i = 0; i < 360; i++)
				{
						x = (float)std::cos(i * 0.01745f);
						y = (float)std::sin(i * 0.01745f);

						float ox = (float)player->x() + 0.5f;
						float oy = (float)player->y() + 0.5f;

						for (int j = 0; j < 40; j++)
						{
								(*current_map->light_map)((int)oy, (int)ox) = 2;

								if ((*current_map->map)((int)oy, (int)ox) == 0) // if tile is a wall
										break;

								ox += x;
								oy += y;
						};
				};
		}
}
