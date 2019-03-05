//Recognised modes for set_shader_mode and get_game_mode
#define WORLDMAP    (0x1)
#define LOCALMAP    (0x2) //No point hooking this: would always be 1 at any point at which scripts are running
#define DIALOG      (0x4)
#define ESCMENU     (0x8)
#define SAVEGAME    (0x10)
#define LOADGAME    (0x20)
#define COMBAT      (0x40)
#define OPTIONS     (0x80)
#define HELP        (0x100)
#define CHARSCREEN  (0x200)
#define PIPBOY      (0x400)
#define PCOMBAT     (0x800)
#define INVENTORY   (0x1000)
#define AUTOMAP     (0x2000)
#define SKILLDEX    (0x4000)
#define INTFACEUSE  (0x8000)
#define INTFACELOOT (0x10000)
#define BARTER      (0x20000)
#define HEROWIN     (0x40000)
#define DIALOGVIEW  (0x80000)

//Valid arguments to register_hook
#define HOOK_TOHIT            (0)
#define HOOK_AFTERHITROLL     (1)
#define HOOK_CALCAPCOST       (2)
#define HOOK_DEATHANIM1       (3)
#define HOOK_DEATHANIM2       (4)
#define HOOK_COMBATDAMAGE     (5)
#define HOOK_ONDEATH          (6)
#define HOOK_FINDTARGET       (7)
#define HOOK_USEOBJON         (8)
#define HOOK_REMOVEINVENOBJ   (9)
#define HOOK_BARTERPRICE      (10)
#define HOOK_MOVECOST         (11)
#define HOOK_HEXMOVEBLOCKING  (12)
#define HOOK_HEXAIBLOCKING    (13)
#define HOOK_HEXSHOOTBLOCKING (14)
#define HOOK_HEXSIGHTBLOCKING (15)
#define HOOK_ITEMDAMAGE       (16)
#define HOOK_AMMOCOST         (17)
#define HOOK_USEOBJ           (18)
#define HOOK_KEYPRESS         (19)
#define HOOK_MOUSECLICK       (20)
#define HOOK_USESKILL         (21)
#define HOOK_STEAL            (22)
#define HOOK_WITHINPERCEPTION (23)
#define HOOK_INVENTORYMOVE    (24)
#define HOOK_INVENWIELD       (25)
#define HOOK_ADJUSTFID        (26)
#define HOOK_COMBATTURN       (27)
#define HOOK_CARTRAVEL        (28)
#define HOOK_SETGLOBALVAR     (29)
#define HOOK_RESTTIMER        (30)
#define HOOK_GAMEMODECHANGE   (31)
#define HOOK_USEANIMOBJ       (32)
#define HOOK_EXPLOSIVETIMER   (33)
#define HOOK_DESCRIPTIONOBJ   (34)
#define HOOK_USESKILLON       (35)
#define HOOK_ONEXPLOSION      (36)
#define HOOK_SUBCOMBATDAMAGE  (37)
#define HOOK_SETLIGHTING      (38)

//Valid arguments to list_begin
#define LIST_CRITTERS    (0)
#define LIST_GROUNDITEMS (1)
#define LIST_SCENERY     (2)
#define LIST_WALLS       (3)
//#define LIST_TILES       (4) //Not listable via sfall list functions
#define LIST_MISC        (5)
#define LIST_SPATIAL     (6)
#define LIST_ALL         (9)

//Valid flags for force_encounter_with_flags
#define ENCOUNTER_FLAG_NO_CAR (1)

//The attack types returned by get_attack_type
#define ATKTYPE_LWEP1           (0)
#define ATKTYPE_LWEP2           (1)
#define ATKTYPE_RWEP1           (2)
#define ATKTYPE_RWEP2           (3)
#define ATKTYPE_PUNCH           (4)
#define ATKTYPE_KICK            (5)
#define ATKTYPE_LWEP_RELOAD     (6)
#define ATKTYPE_RWEP_RELOAD     (7)
#define ATKTYPE_STRONGPUNCH     (8)
#define ATKTYPE_HAMMERPUNCH     (9)
#define ATKTYPE_HAYMAKER       (10)
#define ATKTYPE_JAB            (11)
#define ATKTYPE_PALMSTRIKE     (12)
#define ATKTYPE_PIERCINGSTRIKE (13)
#define ATKTYPE_STRONGKICK     (14)
#define ATKTYPE_SNAPKICK       (15)
#define ATKTYPE_POWERKICK      (16)
#define ATKTYPE_HIPKICK        (17)
#define ATKTYPE_HOOKKICK       (18)
#define ATKTYPE_PIERCINGKICK   (19)

//Some possible values for the 4th argument to hs_removeinvobj
#define RMOBJ_DROP      (0x49B875)  //If the object is dropped manually by the player from the inventory screen
#define RMOBJ_TRADE     (0x47761D)  //If the object is offered up as a trade
#define RMOBJ_DROPMULTI (0x45C1CF)  //When dropping a part of a stack (RMOBJ_DROP occures first)

//Return values for "typeof"
#define VALTYPE_NONE  (0) // not used yet
#define VALTYPE_INT   (1)
#define VALTYPE_FLOAT (2)
#define VALTYPE_STR   (3)

//Arrays defines

// create persistent list
#define create_array_list(size)     (create_array(size, 0))
// create temporary list
#define temp_array_list(size)       (temp_array(size, 0))
// create persistent map
#define create_array_map            (create_array(-1, 0))
// create temporary map
#define temp_array_map              (temp_array(-1, 0))
// create persistent lookup map (see arrays.txt for details)
#define create_lookup_map           (create_array(-1, 2))
// create temporary lookup map
#define temp_lookup_map             (temp_array(-1, 2))
// true if array is map, false otherwise
#define array_is_map(x)             (array_key(x, -1) == 1)
// returns temp list with names of all arrays saved with save_array() in alphabetical order
#define list_saved_arrays           (load_array("...all_arrays..."))
// removes array from savegame
#define unsave_array(x)             save_array(0, x)
// true if given item exists in given array, false otherwise
#define is_in_array(item, array)    (scan_array(array, item) != -1)
// true if given array exists, false otherwise
#define array_exists(array)         (len_array(array) != -1)
// remove all elements from array
#define clear_array(array)          resize_array(array, 0)
// sort array or map by key in ascending order
#define sort_array(array)           resize_array(array, -2)
// sort array or map by key in descending order
#define sort_array_reverse(array)   resize_array(array, -3)
// reverse elements in list/map
#define reverse_array(array)        resize_array(array, -4)
// randomly shuffle elements in list/map
#define shuffle_array(array)        resize_array(array, -5)
// sort map in ascending order by value
#define sort_map_value(array)       resize_array(array, -6)
// sort map in descending order by value
#define sort_map_value_desc(array)  resize_array(array, -7)
// remove element from map or just replace value with 0 for list
#define unset_array(array, item)    set_array(array, item, 0)
// same as "key_pressed" but checks VK codes instead of DX codes
#define key_pressed_vk(key)         (key_pressed(key bwor 0x80000000))

#define set_attack_explosion_pattern(x, y)    metarule2_explosions(1, x, y)
#define set_attack_explosion_art(x, y)        metarule2_explosions(2, x, y)
#define set_attack_explosion_radius(x)        metarule2_explosions(3, x, 0)
#define set_attack_is_explosion(x)            metarule2_explosions(4, x, 0)
#define set_attack_is_explosion_fire          set_attack_is_explosion(DMG_fire)
#define set_explosion_radius(grenade, rocket) metarule2_explosions(5, grenade, rocket)
#define get_explosion_damage(itemPid)         metarule2_explosions(6, itemPid, 0)
#define set_dynamite_damage(minDmg, maxDmg)   metarule2_explosions(7, minDmg, maxDmg)
#define set_plastic_damage(minDmg, maxDmg)    metarule2_explosions(8, minDmg, maxDmg)
#define set_explosion_max_targets(x)          metarule2_explosions(9, x, 0)


#define GAME_MSG_COMBAT      (0)
#define GAME_MSG_AI          (1)
#define GAME_MSG_SCRNAME     (2)
#define GAME_MSG_MISC        (3)
#define GAME_MSG_CUSTOM      (4)
#define GAME_MSG_INVENTRY    (5)
#define GAME_MSG_ITEM        (6)
#define GAME_MSG_LSGAME      (7)
#define GAME_MSG_MAP         (8)
#define GAME_MSG_OPTIONS     (9)
#define GAME_MSG_PERK       (10)
#define GAME_MSG_PIPBOY     (11)
#define GAME_MSG_QUESTS     (12)
#define GAME_MSG_PROTO      (13)
#define GAME_MSG_SCRIPT     (14)
#define GAME_MSG_SKILL      (15)
#define GAME_MSG_SKILLDEX   (16)
#define GAME_MSG_STAT       (17)
#define GAME_MSG_TRAIT      (18)
#define GAME_MSG_WORLDMAP   (19)
#define GAME_MSG_PRO_ITEM   (0x1000)
#define GAME_MSG_PRO_CRIT   (0x1001)
#define GAME_MSG_PRO_SCEN   (0x1002)
#define GAME_MSG_PRO_WALL   (0x1003)
#define GAME_MSG_PRO_TILE   (0x1004)
#define GAME_MSG_PRO_MISC   (0x1005)

#define OUTLINE_NONE        (0)
#define OUTLINE_RED_GLOW    (0x01)
#define OUTLINE_RED         (0x02)
#define OUTLINE_GREY        (0x04)
#define OUTLINE_GREEN_GLOW  (0x08)
#define OUTLINE_YELLOW      (0x10)
#define OUTLINE_DARK_YELLOW (0x20)
#define OUTLINE_PURPLE      (0x40)

#define CURSOR_MOVEMENT     (0)
#define CURSOR_COMMAND      (1)
#define CURSOR_TARGETING    (2)

//Valid flags for set_rest_mode
#define RESTMODE_DISABLED   (1) // disable resting on all maps
#define RESTMODE_STRICT     (2) // disable resting on maps with "can_rest_here=No" in Maps.txt, even if there are no other critters
#define RESTMODE_NO_HEALING (4) // disable healing during resting

#define mstr_combat(x)      (message_str_game(GAME_MSG_COMBAT, x))
#define mstr_ai(x)          (message_str_game(GAME_MSG_AI, x))
#define mstr_scrname(x)     (message_str_game(GAME_MSG_SCRNAME, x))
#define mstr_misc(x)        (message_str_game(GAME_MSG_MISC, x))
#define mstr_custom(x)      (message_str_game(GAME_MSG_CUSTOM, x))
#define mstr_inventry(x)    (message_str_game(GAME_MSG_INVENTRY, x))
#define mstr_item(x)        (message_str_game(GAME_MSG_ITEM, x))
#define mstr_lsgame(x)      (message_str_game(GAME_MSG_LSGAME, x))
#define mstr_map(x)         (message_str_game(GAME_MSG_MAP, x))
#define mstr_options(x)     (message_str_game(GAME_MSG_OPTIONS, x))
#define mstr_perk(x)        (message_str_game(GAME_MSG_PERK, x))
#define mstr_pipboy(x)      (message_str_game(GAME_MSG_PIPBOY, x))
#define mstr_quests(x)      (message_str_game(GAME_MSG_QUESTS, x))
#define mstr_proto(x)       (message_str_game(GAME_MSG_PROTO, x))
#define mstr_script(x)      (message_str_game(GAME_MSG_SCRIPT, x))
#define mstr_skill(x)       (message_str_game(GAME_MSG_SKILL, x))
#define mstr_skilldex(x)    (message_str_game(GAME_MSG_SKILLDEX, x))
#define mstr_stat(x)        (message_str_game(GAME_MSG_STAT, x))
#define mstr_trait(x)       (message_str_game(GAME_MSG_TRAIT, x))
#define mstr_worldmap(x)    (message_str_game(GAME_MSG_WORLDMAP, x))


#define BLOCKING_TYPE_BLOCK     (0)
#define BLOCKING_TYPE_SHOOT     (1)  // use this for more realistic line-of-sight checks
#define BLOCKING_TYPE_AI        (2)
#define BLOCKING_TYPE_SIGHT     (3)  // not really useful (works not as you would expect), game uses this only when checking if you can talk to a person

#define party_member_list_critters      party_member_list(0)
#define party_member_list_all           party_member_list(1)

// FuncX macros
#define add_iface_tag                                  sfall_func0("add_iface_tag")
#define art_cache_clear                                sfall_func0("art_cache_clear")
#define attack_is_aimed                                sfall_func0("attack_is_aimed")
#define car_gas_amount                                 sfall_func0("car_gas_amount")
#define create_win(winName, x, y, w, h)                sfall_func5("create_win", winName, x, y, w, h)
#define create_win_flag(winName, x, y, w, h, flag)     sfall_func6("create_win", winName, x, y, w, h, flag)
#define critter_inven_obj2(obj, type)                  sfall_func2("critter_inven_obj2", obj, type)
#define dialog_message(text)                           sfall_func1("dialog_message", text)
#define dialog_obj                                     sfall_func0("dialog_obj")
#define display_stats                                  sfall_func0("display_stats")
#define exec_map_update_scripts                        sfall_func0("exec_map_update_scripts")
#define floor2(value)                                  sfall_func1("floor2", value)
#define get_can_rest_on_map(map, elev)                 sfall_func2("get_can_rest_on_map", map, elev)
#define get_current_inven_size(obj)                    sfall_func1("get_current_inven_size", obj)
#define get_cursor_mode                                sfall_func0("get_cursor_mode")
#define get_flags(obj)                                 sfall_func1("get_flags", obj)
#define get_ini_section(file, sect)                    sfall_func2("get_ini_section", file, sect)
#define get_ini_sections(file)                         sfall_func1("get_ini_sections", file)
#define get_map_enter_position                         sfall_func0("get_map_enter_position")
#define get_object_ai_data(obj, aiParam)               sfall_func2("get_object_ai_data", obj, aiParam)
#define get_object_data(obj, offset)                   sfall_func2("get_object_data", obj, offset)
#define get_outline(obj)                               sfall_func1("get_outline", obj)
#define get_string_pointer(text)                       sfall_func1("get_string_pointer", text)
#define intface_hide                                   sfall_func0("intface_hide")
#define intface_is_hidden                              sfall_func0("intface_is_hidden")
#define intface_redraw                                 sfall_func0("intface_redraw")
#define intface_show                                   sfall_func0("intface_show")
#define inventory_redraw(invSide)                      sfall_func1("inventory_redraw", invSide)
#define item_make_explosive(pid, aPid, min, max)       sfall_func4("item_make_explosive", pid, aPid, min, max)
#define item_weight(obj)                               sfall_func1("item_weight", obj)
#define lock_is_jammed(obj)                            sfall_func1("lock_is_jammed", obj)
#define loot_obj                                       sfall_func0("loot_obj")
#define npc_engine_level_up                            sfall_func1("npc_engine_level_up", toggle)
#define obj_under_cursor(crSwitch, inclDude)           sfall_func2("obj_under_cursor", crSwitch, inclDude)
#define outlined_object                                sfall_func0("outlined_object")
#define real_dude_obj                                  sfall_func0("real_dude_obj")
#define set_can_rest_on_map(map, elev, value)          sfall_func3("set_can_rest_on_map", map, elev, value)
#define set_car_intface_art(artIndex)                  sfall_func1("set_car_intface_art", artIndex)
#define set_cursor_mode(mode)                          sfall_func1("set_cursor_mode", mode)
#define set_drugs_data(type, pid, value)               sfall_func3("set_drugs_data", type, pid, value)
#define set_dude_obj(critter)                          sfall_func1("set_dude_obj", critter)
#define set_flags(obj, flags)                          sfall_func2("set_flags", obj, flags)
#define set_iface_tag_text(tag, text, color)           sfall_func3("set_iface_tag_text", tag, text, color)
#define set_ini_setting(setting, value)                sfall_func2("set_ini_setting", setting, value)
#define set_map_enter_position(tile, elev, rot)        sfall_func3("set_map_enter_position", tile, elev, rot)
#define set_object_data(obj, offset, value)            sfall_func3("set_object_data", obj, offset, value)
#define set_outline(obj, color)                        sfall_func2("set_outline", obj, color)
#define set_rest_heal_time(time)                       sfall_func1("set_rest_heal_time", time)
#define set_rest_mode(mode)                            sfall_func1("set_rest_mode", mode)
#define set_unjam_locks_time(time)                     sfall_func1("set_unjam_locks_time", time)
#define spatial_radius(obj)                            sfall_func1("spatial_radius", obj)
#define tile_refresh_display                           sfall_func0("tile_refresh_display")
#define unjam_lock(obj)                                sfall_func1("unjam_lock", obj)
