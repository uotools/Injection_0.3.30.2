////////////////////////////////////////////////////////////////////////////////
//
// injection.cpp
//
// Copyright (C) 2001 Luke 'Infidel' Dunstan
//
// Parts from Sniffy.cpp:
// Copyright (C) 2000 Bruno 'Beosil' Heidelberger
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////
//
//  This file contains code that was not present in Sniffy.
//  All code that sends/receives messages directly to the client is here.
//
////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <time.h>
#include "common.h"
#include "hooks.h"
#include "world.h"
#include "equipment.h"
#include "menus.h"
#include "vendor.h"
#include "target.h"
#include "spells.h"
#include "skills.h"
#include "runebook.h"
#include "hotkeyhook.h"

#include "injection.h"
#include "extdll.h"
#include <float.h>

#include "crash.h"

#ifdef __GNUC__
#define DLLEXPORT __attribute__((dllexport))
#else
#define DLLEXPORT
#endif

// Stringification antics
#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define VERSION_STRING STRINGIFY(VERSION) " alpha"

const char COMMAND_PREFIX = ',';

////////////////////////////////////////////////////////////////////////////////

class ServerItem
{
public:
    int id;
    char name[33];
};

class ServerList
{
private:
    ServerItem * m_items;
    int m_num_items;

public:
    ServerList(int num_servers) : m_num_items(num_servers)
    {
        m_items = new ServerItem[num_servers];
    }
    ~ServerList()
    {
        delete [] m_items;
    }

    void set_server(int i, int id, const char * name)
    {
        m_items[i].id = id;
        strncpy(m_items[i].name, name, sizeof(m_items[i].name) - 1);
        m_items[i].name[sizeof(m_items[i].name) - 1] = '\0';
    }

    const char * get_name(int i) const
    {
        return m_items[i].name;
    }

    // Returns the index of the server with the given id,
    // or -1 if the id was not found.
    int find(int id) const
    {
        int i = 0, found = -1;
        while(i < m_num_items && found == -1)
        {
            if(m_items[i].id == id)
                found = i;
            i++;
        }
        return found;
    }
};

class CharacterList
{
private:
    int m_num_slots;
    char (* m_names)[61];

public:
    CharacterList(int num_slots)
    : m_num_slots(num_slots)
    {
        m_names = new char[num_slots][61];
    }

    ~CharacterList()
    {
        delete [] m_names;
    }

    void set_character(int i, const char * name)
    {
        strncpy(m_names[i], name, 60);
        m_names[i][60] = '\0';
    }

    bool valid_index(int i) const
    {
        return i >= 0 && i < m_num_slots;
    }

    const char * get_name(int i) const
    {
        return m_names[i];
    }
};

////////////////////////////////////////////////////////////////////////////////

// An unknown/unverified/obsolete message type
#define UMSG(size) { "?", size, DIR_BOTH, 0, 0 }
// A message type sent to the server
#define SMSG(name, size) { name, size, DIR_SEND, 0, 0 }
// A message type received from the server
#define RMSG(name, size) { name, size, DIR_RECV, 0, 0 }
// A message type transmitted in both directions
#define BMSG(name, size) { name, size, DIR_BOTH, 0, 0 }
// Message types that have handler methods
#define SMSGH(name, size, smethod) \
    { name, size, DIR_SEND, &Injection::smethod, 0 }
#define RMSGH(name, size, rmethod) \
    { name, size, DIR_RECV, 0, &Injection::rmethod }
#define BMSGH(name, size, smethod, rmethod) \
    { name, size, DIR_BOTH, &Injection::smethod, &Injection::rmethod }

MessageType Injection::m_message_types[NUM_MESSAGE_TYPES] =
{
    SMSG("Create Character", 0x68), // 0x00
    SMSG("Disconnect", 0x05),
    SMSGH("Walk Request", 0x07, handle_walk_request),
    SMSGH("Client Talk", SIZE_VARIABLE, handle_client_talk),
    UMSG(0x02),
    SMSG("Attack", 0x05),
    SMSG("Double Click", 0x05),
    SMSG("Pick Up Item", 0x07),
    SMSG("Drop Item", 0x0e), // 0x08
    SMSG("Single Click", 0x05),
    UMSG(0x0b),
    UMSG(0x10a),
    UMSG(SIZE_VARIABLE),
    UMSG(0x03),
    UMSG(SIZE_VARIABLE),
    UMSG(0x3d),
    UMSG(0xd7), // 0x10
    RMSGH("Character Status", SIZE_VARIABLE, handle_character_status),
    SMSG("Perform Action", SIZE_VARIABLE),
    SMSG("Client Equip Item", 0x0a),
    UMSG(0x06),
    UMSG(0x09),
    UMSG(0x01),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE), // 0x18
    UMSG(SIZE_VARIABLE),
    RMSGH("Update Item", SIZE_VARIABLE, handle_update_item),
    RMSGH("Enter World", 0x25, handle_enter_world),
    RMSGH("Server Talk", SIZE_VARIABLE, handle_server_talk),
    RMSGH("Delete Object", 0x05, handle_delete_object),
    UMSG(0x04),
    UMSG(0x08),
    RMSGH("Update Player", 0x13, handle_update_player), // 0x20
    RMSG("Deny Walk", 0x08),
    BMSG("Confirm Walk", 0x03),
    RMSG("Drag Animation", 0x1a),
    RMSGH("Open Container", 0x07, handle_open_container),  // 0x24
    RMSGH("Update Contained Item", 0x14, handle_update_contained_item), //0x25
    UMSG(0x05),
    RMSG("Deny Move Item", 0x02),
    UMSG(0x05), // 0x28
    UMSG(0x01),
    UMSG(0x05),
    UMSG(0x02),
    BMSG("Death Dialog", 0x02),
    UMSG(0x11),
    RMSGH("Server Equip Item", 0x0f, handle_server_equip_item),
    RMSG("Combat Notification", 0x0a),
    UMSG(0x05), // 0x30
    UMSG(0x01),
    UMSG(0x02),
    RMSGH("Pause Control", 0x02, handle_pause_control),
    SMSGH("Status Request", 0x0a, handle_status_request),
    UMSG(0x28d),
    UMSG(SIZE_VARIABLE),
    UMSG(0x08),
    UMSG(0x07), // 0x38
    UMSG(0x09),
    BMSG("Update Skills", SIZE_VARIABLE),
    BMSGH("Vendor Buy Reply", SIZE_VARIABLE, handle_vendor_buy_reply_s,
        handle_vendor_buy_reply_r),
    RMSGH("Update Contained Items", SIZE_VARIABLE,
        handle_update_contained_items),    //ox3c
    UMSG(0x02),
    UMSG(0x25),
    UMSG(SIZE_VARIABLE),
    UMSG(0xc9), // 0x40
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    UMSG(0x229),
    UMSG(0x2c9),
    UMSG(0x05),
    UMSG(SIZE_VARIABLE),
    UMSG(0x0b),
    UMSG(0x49), // 0x48
    UMSG(0x5d),
    UMSG(0x05),
    UMSG(0x09),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSG("Object Light Level", 0x06),
    RMSGH("Global Light Level", 0x02, handle_global_light_level),
    UMSG(SIZE_VARIABLE), // 0x50
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSGH("Error Code", 0x02, handle_error_code),   // Idle message
    RMSG("Sound Effect", 0x0c),
    RMSG("Login Complete", 0x01),
    BMSG("Map Data", 0x0b),
    UMSG(0x6e),
    UMSG(0x6a), // 0x58
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSG("Set Time?", 0x04),
    UMSG(0x02),
    SMSGH("Select Character", 0x49, handle_select_character),
    UMSG(SIZE_VARIABLE),
    UMSG(0x31),
    UMSG(0x05), // 0x60
    UMSG(0x09),
    UMSG(0x0f),
    UMSG(0x0d),
    UMSG(0x01),
    RMSGH("Set Weather", 0x04, handle_weather_change),
    BMSG("Book Page Data", SIZE_VARIABLE),
    UMSG(0x15),
    UMSG(SIZE_VARIABLE), // 0x68
    UMSG(SIZE_VARIABLE),
    UMSG(0x03),
    UMSG(0x09),
    BMSGH("Target Data", 0x13, handle_target_s, handle_target_r),
    RMSG("Play Music", 0x03),
    RMSG("Character Animation", 0x0e),
    BMSG("Secure Trading", SIZE_VARIABLE),
    RMSG("Graphic Effect", 0x1c), // 0x70
    BMSG("Message Board Data", SIZE_VARIABLE),
    BMSG("War Mode", 0x05),
    BMSG("Ping", 0x02),
    RMSGH("Vendor Buy List", SIZE_VARIABLE, handle_vendor_buy_list),
    SMSG("Rename Character", 0x23),
    UMSG(0x10),
    RMSG("Update Character", 0x11),
    RMSGH("Update Object", SIZE_VARIABLE, handle_update_object), // 0x78
    UMSG(0x09),
    UMSG(SIZE_VARIABLE),
    UMSG(0x02),
    RMSGH("Open Menu Gump", SIZE_VARIABLE, handle_open_menu_gump),
    SMSG("Menu Choice", 0x0d),
    UMSG(0x02),
    UMSG(SIZE_VARIABLE),
    SMSGH("First Login", 0x3e, handle_first_login), // 0x80
    UMSG(SIZE_VARIABLE),
    RMSG("Login Error", 0x02),
    SMSG("Delete Character", 0x27),
    UMSG(0x45),
    UMSG(0x02),
    RMSGH("Character List 2", SIZE_VARIABLE, handle_character_list2),
    UMSG(SIZE_VARIABLE),
    RMSG("Open Paperdoll", 0x42), // 0x88
    RMSG("Corpse Equipment", SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSGH("Relay Server", 0x0b, handle_relay_server),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSG("Display Map", 0x13), // 0x90
    SMSGH("Second Login", 0x41, handle_second_login),
    UMSG(SIZE_VARIABLE),
    RMSG("Open Book", 0x63),
    UMSG(SIZE_VARIABLE),
    BMSGH("Dye Data", 0x09, handle_dye_s, handle_dye_r),
    UMSG(SIZE_VARIABLE),
    UMSG(0x02),
    UMSG(SIZE_VARIABLE), // 0x98
    BMSG("Multi Placement", 0x1a),
    UMSG(SIZE_VARIABLE),
    SMSG("Help Request", 0x102),
    UMSG(0x135),
    UMSG(0x33),
    RMSGH("Vendor Sell List", SIZE_VARIABLE, handle_vendor_sell_list),
    SMSGH("Vendor Sell Reply", SIZE_VARIABLE, handle_vendor_sell_reply),
    SMSGH("Select Server", 0x03, handle_select_server), // 0xa0
    RMSGH("Update Hitpoints", 0x09, handle_update_hitpoints),
    RMSGH("Update Mana", 0x09, handle_update_mana),
    RMSGH("Update Stamina", 0x09, handle_update_stamina),
    SMSG("System Information", 0x95),
    RMSG("Open URL", SIZE_VARIABLE),
    RMSG("Tip Window", SIZE_VARIABLE),
    SMSG("Request Tip", 0x04),
    RMSGH("Server List", SIZE_VARIABLE, handle_server_list), // 0xa8
    RMSGH("Character List", SIZE_VARIABLE, handle_character_list),
    RMSG("Attack Reply", 0x05),
    RMSG("Text Input Dialog", SIZE_VARIABLE),
    SMSG("Text Input Reply", SIZE_VARIABLE),
    SMSGH("Unicode Client Talk", SIZE_VARIABLE, handle_unicode_client_talk),
    RMSG("Unicode Server Talk", SIZE_VARIABLE),
    UMSG(0x0d),
    RMSGH("Open Dialog Gump", SIZE_VARIABLE, handle_open_gump), // 0xb0
    SMSG("Dialog Choice", SIZE_VARIABLE),
    BMSG("Chat Data", SIZE_VARIABLE),
    RMSG("Chat Text ?", SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    RMSG("Open Chat Window", 0x40),
    SMSG("Popup Help Request", 0x09),
    RMSG("Popup Help Data", SIZE_VARIABLE),
    BMSG("Character Profile", SIZE_VARIABLE), // 0xb8
    RMSG("Chat Enable", 0x03),
    RMSG("Display Guidance Arrow", 0x06),
    SMSG("Account ID ?", 0x09),
    RMSG("Season ?", 0x03),
    SMSG("Client Version", SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    BMSG("New Commands", SIZE_VARIABLE),
    UMSG(0x24), // 0xc0
    RMSG("Display cliloc String", SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    UMSG(SIZE_VARIABLE),
    UMSG(0x06),
    UMSG(0xcb),
    UMSG(0x01),
    UMSG(0x31),
    UMSG(0x02), // 0xc8
    UMSG(0x06),
    UMSG(0x06),
    UMSG(0x07),
    UMSG(SIZE_VARIABLE),
};

////////////////////////////////////////////////////////////////////////////////

#pragma warning( disable: 4355 )	// 'this' used in base member init
Injection::Injection()
: m_gui(*this, m_config), m_counter_manager(m_gui, m_character),
  m_hook(0),
  m_servers(0), m_server_id(-1), m_server(0),
  m_account(0),
  m_characters(0), m_character(0),
  m_world(0), m_hotkeyhook(0),
  m_dress_handler(0), m_menu_handler(0), m_vendor_handler(0), m_targeting_handler(0),
  m_runebook_handler(0),
  m_spells(0), m_skills(0), m_normal_light(0), m_dye_colour(-1),
  m_targeting(false), m_client_targeting(false), m_last_target_set(false),
  m_target_handler(0),m_use_tab_dialog(0),m_use_target_handler(0),
  m_object_tab_dialog(0),m_object_target_handler(0),
  m_receiving_container(0),  empty_speed(0), m_backpack(0), m_backpack_set(false),
  m_catchbag(0), m_catchbag_set(false), m_lastcaught(0)
{
    m_hook_set=new SocketHookSet(*this);
    m_spells = new Spells(*this, *m_targeting_handler);
    m_skills = new Skills(*this, *m_targeting_handler);
}

Injection::~Injection()
{
    delete m_hook_set;
    delete m_vendor_handler;
    delete m_menu_handler;
    delete m_runebook_handler;
    delete m_targeting_handler;
    delete m_dress_handler;
    delete m_hotkeyhook;
    delete m_world;
    delete m_characters;
    delete m_servers;
    delete m_spells;
    delete m_skills;
    m_config.save();
    log_flush();    // DEBUG
}

// This function from Sniffy.cpp
int Injection::init(unsigned int /*checksum*/, unsigned int /*length*/)
{
    if(!m_config.load("injection.xml"))
    {
        log_flush();
        return INJECTION_ERROR_CONFIG;
    }
    m_hook_set->install();
    if(!m_gui.init())
    {
        log_flush();
        return INJECTION_ERROR_GUI;
    }
    return INJECTION_ERROR_NONE;
}

////////////////////////////////////////////////////////////////////////////////

//// Methods of HookCallbackInterface:

int Injection::get_message_size(int code)
{
    if(code < 0 || code >= NUM_MESSAGE_TYPES)
        return -1;
    return m_message_types[code].size;
}

void Injection::disconnected(SocketHook * hook)
{
    if(m_hook == hook)
    {
        delete m_vendor_handler;
        m_vendor_handler = 0;
        delete m_menu_handler;
        m_menu_handler = 0;
        delete m_runebook_handler;
        m_runebook_handler = 0;
        delete m_targeting_handler;
        m_targeting_handler = 0;
        delete m_dress_handler;
        m_dress_handler = 0;
        m_counter_manager.disconnected();
        m_gui.disconnected();
        delete m_world;
        m_world = 0;
        m_character = 0;
        delete m_characters;
        m_characters = 0;
        //m_account = 0;
        //m_server = 0;
        //m_server_id = -1;
        //delete m_servers;
        //m_servers = 0;
        m_hook = 0;
        m_backpack_set = false;
        m_catchbag_set = false;
        m_last_target_set = false;
        m_receiving_container = 0;
    }
}

void Injection::handle_key(SocketHook * /*hook*/, uint8 key[4])
{
    // SphereClient sends a key of all zeroes to signify that encryption
    // is not enabled.
    if(m_config.get_encryption() == ENCRYPTION_SPHERECLIENT)
        memset(key, 0, 4);
}

bool Injection::handle_send_message(SocketHook * hook, uint8 * buf, int size)
{
    m_hook = hook;
    trace_printf("-------------------- client --------------------\n");
    if(*buf >= NUM_MESSAGE_TYPES)
    {
        error_printf("Unknown message type: 0x%02x\n", *buf);
        trace_dump(buf, size);
    }
    else
    {
        MessageType & type = m_message_types[*buf];
        trace_dump(buf, size);
        if(type.direction != DIR_SEND && type.direction != DIR_BOTH)
            warning_printf("message direction invalid: 0x%02X\n", *buf);
        else if(type.shandler != 0)
            return (this ->* (type.shandler))(buf, size);
    }
    return true;
}

bool Injection::handle_receive_message(SocketHook * hook, uint8 * buf, int size)
{
    m_hook = hook;
    trace_printf("-------------------- server --------------------\n");
    if(*buf >= NUM_MESSAGE_TYPES)
    {
        error_printf("Unknown message type: 0x%02x\n", *buf);
        trace_dump(buf, size);
    }
    else
    {
        MessageType & type = m_message_types[*buf];
        trace_dump(buf, size);
        if(type.direction != DIR_RECV && type.direction != DIR_BOTH)
            warning_printf("message direction invalid: 0x%02X\n", *buf);
        else if(type.rhandler != 0)
            return (this ->* (type.rhandler))(buf, size);
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

//// Message handlers:
// (all private)

bool Injection::handle_walk_request(uint8 * buf, int size)
{
    if(m_server != 0 && m_server->get_fixwalk())
    {
        // Send the message minus the last 4 bytes
        send_server(buf, size - 4);
        return false;
    }
    return true;
}

bool Injection::handle_client_talk(uint8 * buf, int /*size*/)
{
    trace_printf("text: %s\n", buf + 8);
    if(buf[8] == COMMAND_PREFIX)
    {
        do_command(reinterpret_cast<char *>(buf + 9));
        return false;
    }
    return true;
}

bool Injection::handle_character_status(uint8 * buf, int /*size*/)
{
    if(m_world == 0)
        return true;
    uint32 serial = unpack_big_uint32(buf + 3);
    if(serial != m_world->get_player()->get_serial())
    {
        char name[31];
        memcpy(name, buf + 7, 30);
        name[30] = '\0';
        uint16 hp = unpack_big_uint16(buf + 37);
        uint16 maxhp = unpack_big_uint16(buf + 39);
        char buf[100];
        sprintf(buf, "%s has %d/%d hitpoints", name, hp, maxhp);
        client_print(buf);
    }
    else  //its our char
    {
        m_counter_manager.set_m_hp(unpack_big_uint16(buf + 37));
        m_counter_manager.set_m_max_hp(unpack_big_uint16(buf + 39));
        m_counter_manager.set_m_mana(unpack_big_uint16(buf +54));
        m_counter_manager.set_m_max_mana(unpack_big_uint16(buf +56));
        m_counter_manager.set_m_stamina(unpack_big_uint16(buf +50));
        m_counter_manager.set_m_max_stamina(unpack_big_uint16(buf +52));
        m_counter_manager.set_m_ar(unpack_big_uint16(buf +62));
        m_counter_manager.set_m_weight(unpack_big_uint16(buf +64));
        m_counter_manager.set_m_gold(unpack_big_uint32(buf +58));
        m_counter_manager.update();
    }
    return true;
}

bool Injection::handle_update_hitpoints(uint8 * buf, int /*size*/)
{
    if(m_world == 0) return true;
    uint32 serial = unpack_big_uint32(buf + 1);
    if(serial == m_world->get_player()->get_serial())
    {
        m_counter_manager.set_m_max_hp(unpack_big_uint16(buf + 5));
        m_counter_manager.set_m_hp(unpack_big_uint16(buf + 7));
        m_counter_manager.update();
    }
    return true;
}

bool Injection::handle_update_mana(uint8 * buf, int /*size*/)
{
    if(m_world == 0) return true;
    uint32 serial = unpack_big_uint32(buf + 1);
    if(serial == m_world->get_player()->get_serial())
    {
        m_counter_manager.set_m_max_mana(unpack_big_uint16(buf + 5));
        m_counter_manager.set_m_mana(unpack_big_uint16(buf + 7));
        m_counter_manager.update();
    }
    return true;
}

bool Injection::handle_update_stamina(uint8 * buf, int /*size*/)
{
    if(m_world == 0) return true;
    uint32 serial = unpack_big_uint32(buf + 1);
    if(serial == m_world->get_player()->get_serial())
    {
        m_counter_manager.set_m_max_stamina(unpack_big_uint16(buf + 5));
        m_counter_manager.set_m_stamina(unpack_big_uint16(buf + 7));
        m_counter_manager.update();
    }
    return true;
}

bool Injection::handle_update_item(uint8 * buf, int size)
{
    if(m_world == 0)
        return true;
// 1a [2 bytes length] [4 bytes serial] [2 bytes graphic] [2 bytes X] [2 bytes Y | 4000h sometimes] [1 byte Z] [unk, may be absent]

    GameObject * obj = m_world->get_object(unpack_big_uint32(buf + 3));
    obj->set_x(buf + 9);
    obj->set_y(buf + 11);
    obj->set_z(buf + 12);
    obj->set_graphic(buf + 7);

    return true;
}

bool Injection::handle_enter_world(uint8 * buf, int /*size*/)
{
    uint32 serial = unpack_big_uint32(buf + 1);
    if(m_account == 0)
        error_printf("Entered world with unknown account!\n");
    else
        m_character = m_account->get(serial);
    if(m_world != 0)
    {
        error_printf("duplicate enter world message\n");
        ASSERT(m_vendor_handler != 0);
    }
    else
    {
        ASSERT(m_vendor_handler == 0);
        m_world = new World(serial);
        m_counter_manager.connected();
        m_gui.connected(m_character);
        m_vendor_handler = new VendorHandler(m_config, *this, *m_world, *m_server);
        m_menu_handler = new MenuHandler(*this);
        m_targeting_handler = new TargetHandler(*this);
        m_runebook_handler = new RunebookHandler(*this, *m_targeting_handler);
        if(m_spells)
            delete m_spells;
        m_spells = new Spells(*this, *m_targeting_handler);
        if(m_skills)
            delete m_skills;
        m_skills = new Skills(*this, *m_targeting_handler);
    }
    GameObject * player = m_world->get_player();
    player->set_graphic(buf + 9);
    player->set_x(buf + 11);
    player->set_y(buf + 13);
    player->set_z(buf + 16);
    player->set_direction(buf + 17);
    //player->set_flags(buf + 28);
    trace_printf("Player 0x%08lX entered the world.\n", serial);

    delete m_dress_handler;
    if(m_character != 0)
    {
        m_dress_handler = new DressHandler(*this, *player, *m_character);
        m_hotkeyhook = new HotkeyHook(*this, m_character->m_hotkeys);
    }
    else
    {
        m_dress_handler = 0;
        m_hotkeyhook = 0;
    }

    if(m_hotkeyhook != 0)
        if(!m_hotkeyhook->install_hook(g_hinstance, 0))
            trace_printf("Hotkey Hook not installed");
    return true;
}

bool Injection::handle_delete_object(uint8 * buf, int /*size*/)
{
    if(m_world != 0)
    {
        uint32 serial = unpack_big_uint32(buf + 1);
        GameObject * obj = m_world->find_object(serial);
        if(obj != 0)
            m_world->remove_object(obj);
    }
    return true;
}

bool Injection::handle_update_player(uint8 * buf, int /*size*/)
{
    if(m_world == 0)
        return true;
    uint32 serial = unpack_big_uint32(buf + 1);
    GameObject * player = m_world->get_player();
    if(serial != player->get_serial())
    {
        warning_printf("Current player changed from 0x%08lX to 0x%08lX\n",
            player->get_serial(), serial);
        m_world->set_player(serial);
        player = m_world->get_player();
//      if(m_hotkeyhook !=0)
//          m_hotkeyhook->remove_hook();
//      m_hotkeyhook = new HotkeyHook(*this, m_character->m_hotkeys);
//      if(m_hotkeyhook != 0)
//          if(!m_hotkeyhook->install_hook(g_hinstance, 0))
//              trace_printf("Hotkey Hook not installed");

    }
    player->set_graphic(buf + 5);
    player->set_colour(buf + 8);
    //player->set_flags(buf + 10);
    player->set_x(buf + 11);
    player->set_y(buf + 13);
    player->set_direction(buf + 17);
    player->set_z(buf + 18);
    return true;
}

bool Injection::handle_open_container(uint8 * buf, int size)
{
    if(m_vendor_handler != 0)
        return m_vendor_handler->handle_open_container(buf, size);
    return true;
}

bool Injection::handle_update_contained_item(uint8 * buf, int /*size*/)
{
    if(m_world == 0)
        return true;
    GameObject * obj = m_world->get_object(unpack_big_uint32(buf + 1));
    uint32 cserial = unpack_big_uint32(buf + 14);
    m_world->put_container(obj, cserial);
    m_counter_manager.set_object_graphic(obj, buf + 5);
    obj->set_quantity(buf + 8);
    obj->set_x(buf + 10);
    obj->set_y(buf + 12);
    obj->set_colour(buf + 18);
    // handle catchbag
    if((m_catchbag_set) && (cserial == m_backpack)){
        if(obj->get_serial() != m_lastcaught)
        {
            m_lastcaught = obj->get_serial();
            move_container(obj->get_serial(), obj->get_quantity(), m_catchbag);
        }
    }
    return true;
}

bool Injection::handle_server_equip_item(uint8 * buf, int /*size*/)
{
    if(m_world == 0)
        return true;
    uint32 cserial = unpack_big_uint32(buf + 9);
    int layer = buf[8];
    // We are only interested in what the current player is wearing,
    // and any containers used for buying/selling.
    if(layer == LAYER_VENDOR_BUY_RESTOCK || layer == LAYER_VENDOR_BUY ||
            layer == LAYER_VENDOR_SELL ||
            cserial == m_world->get_player()->get_serial())
    {
        GameObject * obj = m_world->get_object(unpack_big_uint32(buf + 1));
        m_counter_manager.set_object_graphic(obj, buf + 5);
        obj->set_colour(buf + 13);
        m_world->put_equipment(obj, cserial, layer);
    }
    return true;
}

bool Injection::handle_pause_control(uint8 * /*buf*/, int /*size*/)
{
    return true;
}

bool Injection::handle_status_request(uint8 * /*buf*/, int /*size*/)
{
//  uint32 serial = unpack_big_uint32(buf + 6);
    return true;
}

bool Injection::handle_global_light_level(uint8 * buf, int /*size*/)
{
    m_normal_light = buf[1];
    // Return true if the light level is not permanently fixed.
    return m_character == 0 || m_character->get_light() == LIGHT_NORMAL;
}

bool Injection::handle_error_code(uint8 * buf, int /*size*/)
{
    if(buf[1] == 7)     // Idle warning
    {
        // Prevent idle logout
        uint8 sbuf[5];
        sbuf[0] = 0x09; // Single click
        pack_big_uint32(sbuf + 1, 0);   // serial
        send_server(sbuf, sizeof(sbuf));
        return false;
    }
    return true;
}

bool Injection::handle_select_character(uint8 * buf, int /*size*/)
{
    int character_index = buf[68];
    if(m_characters == 0)
        error_printf("selected character #%d from unknown list\n",
            character_index);
    else if(!m_characters->valid_index(character_index))
        warning_printf("selected invalid character #%d\n", character_index);
    else
    {
        char sel_name[64];
        const char * name = m_characters->get_name(character_index);
        trace_printf("Selected character named: %s\n", name);
        strncpy(sel_name, reinterpret_cast<char *>(buf + 5), 63);
        sel_name[63] = '\0';
        if(strcmp(sel_name, name) != 0)
            warning_printf("selected character name '%s' != '%s'\n",
                sel_name, name);
    }
    return true;
}

bool Injection::handle_weather_change(uint8 * /*buf*/, int /*size*/)
{
    if(m_server != 0 && m_server->get_filter_weather())
    {
        // eat the message
        return false;
    }
    return true;
}

bool Injection::handle_target_s(uint8 * buf, int /*size*/)
{
    // store last non self object target
    uint32 serial = unpack_big_uint32(buf + 7);
    if((serial != m_world->get_player()->get_serial()) && (serial != 0) && (buf[1] == 0)){
        m_last_target = serial;
        m_last_target_set = true;
    }
    if(m_targeting)
    {
        GameObject * obj = 0;
        //if(buf[1] != 1)
        //  error_printf("target mode should be 1\n");
        if(buf[6] != 0)
            error_printf("target type should be object (0)\n");
        // If serial == 0, the user cancelled it.
        else if(m_world != 0 && serial != 0)
        {
            obj = m_world->get_object(serial);
            if(obj->get_graphic() == 0) // Object graphic is unknown
                obj->set_graphic(buf + 17);
        }
        got_target(obj);
        return false;
    }
    else if(m_client_targeting)
        m_client_targeting = false;
    return true;
}

bool Injection::handle_target_r(uint8 * buf, int size)
{
    // If the server sends a target request, cancel our internal targeting.
    if(m_targeting)
        got_target(0);
    m_client_targeting = true;
    // Remember the parameters in case we need to cancel it.
    memcpy(m_cancel_target, buf, sizeof(m_cancel_target));
    pack_big_uint32(buf + 7, 0);    // serial
    pack_big_uint16(buf + 11, 0xffff);    // x
    pack_big_uint16(buf + 13, 0xffff);    // y
    return m_targeting_handler->handle_target(buf, size);
}

bool Injection::handle_vendor_buy_list(uint8 * buf, int size)
{
    if(m_vendor_handler != 0)
        return m_vendor_handler->handle_vendor_buy_list(buf, size);
    return true;
}

bool Injection::handle_vendor_buy_reply_s(uint8 * /*buf*/, int /*size*/)
{
    return true;
}

bool Injection::handle_vendor_buy_reply_r(uint8 * /*buf*/, int /*size*/)
{
    return true;
}

bool Injection::handle_update_contained_items(uint8 * buf, int size)
{
    if(m_world == 0)
        return true;
    uint16 count = unpack_big_uint16(buf + 3);
    uint8 * ptr = buf + 5;
    for(int i = 0; i < count; i++)
    {
        GameObject * obj = m_world->get_object(unpack_big_uint32(ptr));
        uint32 cserial = unpack_big_uint32(ptr + 13);
        m_world->put_container(obj, cserial);
        m_counter_manager.set_object_graphic(obj, ptr + 4);
        obj->set_quantity(ptr + 7);
        obj->set_x(ptr + 9);
        obj->set_y(ptr + 11);
        obj->set_colour(ptr + 17);
        ptr += 19;
    }
    if(size != ptr - buf)
        warning_printf("update_contained_items size should be %d\n",
            ptr - buf);
    return true;
}

bool Injection::handle_update_object(uint8 * buf, int size)
{
    if(m_world == 0)
        return true;
    uint32 serial = unpack_big_uint32(buf + 3);
    // Again, we are only interested in the current player.
    if(serial != m_world->get_player()->get_serial())
        return true;
    GameObject * obj = m_world->get_object(serial & 0x7fffffff);
    uint16 graphic = unpack_big_uint16(buf + 7);
    m_counter_manager.set_object_graphic(obj, graphic & 0x7fff);
    uint8 * ptr = buf + 9;
    if(serial & 0x80000000)
    {
        obj->set_quantity(ptr);
        ptr += 2;
    }
    if(graphic & 0x8000)
    {
        obj->set_increment(ptr);
        ptr += 2;
    }
    uint16 x = unpack_big_uint16(ptr);
    obj->m_x = x & 0x7fff;
    ptr += 2;
    obj->set_y(ptr);
    ptr += 2;
    if(x & 0x8000)
    {
        //obj->set_direction2(ptr);
        ptr++;
    }
    obj->set_z(ptr++);
    obj->set_direction(ptr++);
    obj->set_colour(ptr);
    ptr += 2;
    //obj->set_flags(ptr);
    ptr++;
    obj->set_notoriety(ptr++);
    serial = unpack_big_uint32(ptr);
    ptr += 4;
    while(serial != 0)
    {
        GameObject * obj2 = m_world->get_object(serial);
        graphic = unpack_big_uint16(ptr);
        m_counter_manager.set_object_graphic(obj2, graphic & 0x7fff);
        ptr += 2;
        int layer = *ptr++;
        if(graphic & 0x8000)
        {
            obj2->set_colour(ptr);
            ptr += 2;
        }
        m_world->put_equipment(obj2, obj, layer);
        serial = unpack_big_uint32(ptr);
        ptr += 4;
    }
    if(size != ptr - buf)
        warning_printf("update_object size should be %d\n", ptr - buf);
    return true;
}

bool Injection::handle_open_menu_gump(uint8 * buf, int size)
{
    if(m_menu_handler != 0)
        return m_menu_handler->handle_open_menu_gump(buf, size);
    return true;
}

const int LOGINKEY1_V1_26_4 = 0x32750719;
const int LOGINKEY2_V1_26_4 = 0x0a2d100b;
const int LOGINKEY1_V2_0_0 = 0x2d13a5fd;
const int LOGINKEY2_V2_0_0 = 0xa39d527f;
const int LOGINKEY1_V3_0_5 = /*0x2d13a5fd;  */ 0x2c8b97ad;
const int LOGINKEY2_V3_0_5 = /*0xa39d527f;  */ 0xa350de7f;

bool Injection::handle_first_login(uint8 * /*buf*/, int /*size*/)
{
    m_hook->set_compressed(false);  // probably not really necessary
    switch(m_config.get_encryption())
    {
    case ENCRYPTION_IGNITION:
    case ENCRYPTION_SPHERECLIENT:
        // Do nothing.
        break;
    case ENCRYPTION_1_26_4:
        m_hook->set_login_encryption(LOGINKEY1_V1_26_4, LOGINKEY2_V1_26_4);
        break;
    case ENCRYPTION_2_0_0:
        m_hook->set_login_encryption(LOGINKEY1_V2_0_0, LOGINKEY2_V2_0_0);
        break;
    case ENCRYPTION_3_0_5:
        m_hook->set_login_encryption(LOGINKEY1_V3_0_5, LOGINKEY2_V3_0_5);
        break;
    default:
        FATAL("Invalid encryption config");
    }
    return true;
}

bool Injection::handle_character_list2(uint8 * buf, int /*size*/)
{
    int num_slots = buf[3];
    delete m_characters;
    m_characters = 0;
    if(num_slots == 0)
        trace_printf("Warning: no slots in character list\n");
    else
    {
        m_characters = new CharacterList(num_slots);
        for(int i = 0; i < num_slots; i++)
            m_characters->set_character(i,
                reinterpret_cast<char *>(buf + 4 + i * 60));
    }
    return true;
}

bool Injection::handle_relay_server(uint8 * /*buf*/, int /*size*/)
{
    m_server = 0;
    if(m_server_id == -1)
        error_printf("relayed to unknown server id\n");
    else
    {
        ASSERT(m_servers != 0);
        int server_index = m_servers->find(m_server_id);
        if(server_index == -1)
            warning_printf("selected server id not in list\n");
        else
        {
            string server_name(m_servers->get_name(server_index));
            trace_printf("Relayed to server named: %s\n",
                server_name.c_str());
            // Lookup the configuration for this server
            if(!ConfigManager::valid_key(server_name))
                warning_printf("server name has strange characters.\n");
            m_server = m_config.get(server_name);
        }
    }
    return true;
}

bool Injection::handle_second_login(uint8 * buf, int /*size*/)
{
    m_hook->set_compressed(true);
    switch(m_config.get_encryption())
    {
    case ENCRYPTION_IGNITION:
    case ENCRYPTION_SPHERECLIENT:
        // Do nothing.
        break;
    case ENCRYPTION_1_26_4:
    case ENCRYPTION_2_0_0:
    case ENCRYPTION_3_0_5:
        m_hook->set_game_encryption(m_config.get_encryption());
        break;
    default:
        FATAL("Invalid encryption config");
    }

    char account_name[31];
    strncpy(account_name, reinterpret_cast<char *>(buf + 5),
        sizeof(account_name) - 1);
    // Add null terminator
    account_name[sizeof(account_name) - 1] = '\0';
    trace_printf("Logging in with account name: %s\n", account_name);

    // Lookup the configuration data for this account
    if(m_server == 0)
        error_printf("Logging in to unknown server\n");
    else
    {
        string str(account_name);
        if(!ConfigManager::valid_key(str))
            warning_printf("account name has invalid characters.");
        m_account = m_server->get(str);
    }
    return true;
}

bool Injection::handle_dye_s(uint8 * buf, int /*size*/)
{
    if(m_dye_colour != -1)
    {
        client_print("Dye colour overridden.");
        pack_big_uint16(buf + 7, m_dye_colour);
        m_dye_colour = -1;
    }
    return true;
}

bool Injection::handle_dye_r(uint8 * buf, int size)
{
    if(m_dye_colour != -1)
    {
        client_print("Dye colour overridden.");
        // Copy the dye tub graphic index to the new offset.
        pack_big_uint16(buf + 5, unpack_big_uint16(buf + 7));
        pack_big_uint16(buf + 7, m_dye_colour);
        m_dye_colour = -1;
        // Send back to server.
        send_server(buf, size);
        return false;
    }
    return true;
}

bool Injection::handle_vendor_sell_list(uint8 * buf, int size)
{
    if(m_vendor_handler != 0)
        return m_vendor_handler->handle_vendor_sell_list(buf, size);
    return true;
}

bool Injection::handle_vendor_sell_reply(uint8 * /*buf*/, int /*size*/)
{
    return true;
}

bool Injection::handle_select_server(uint8 * buf, int /*size*/)
{
    m_server_id = -1;
    if(m_servers == 0)
        error_printf("selected server #%d from unknown list\n", m_server_id);
    else
    {
        m_server_id = unpack_big_uint16(buf + 1);
        trace_printf("Selected server #%d\n", m_server_id);
    }
    return true;
}

bool Injection::handle_server_list(uint8 * buf, int size)
{
    // We need to remember the server list so that we know the name of
    // the server when the relay message comes back from the server.
    int num_servers = unpack_big_uint16(buf + 4);
    if(num_servers == 0)
    {
        warning_printf("empty server list\n");
        return true;
    }
    delete m_servers;
    m_servers = new ServerList(num_servers);

    // Calculate expected message size
    int ex_size = 6 + num_servers * 40;
    if(size != ex_size)
        warning_printf("server list message size should be %d\n",
            ex_size);

    uint8 * ptr = buf + 6;
    for(int i = 0; i < num_servers; i++)
    {
        m_servers->set_server(i, unpack_big_uint16(ptr),
            reinterpret_cast<char *>(ptr + 2));
        ptr += 40;
    }
    return true;
}

bool Injection::handle_character_list(uint8 * buf, int /*size*/)
{
    int num_slots = buf[3];
    return handle_character_list2(buf, 5 + num_slots * 60);
}

bool Injection::handle_unicode_client_talk(uint8 * buf, int size)
{
    bool resend = true;
    int mode = buf[3];
    if((mode & 0xc0) == 0xc0)    // special command
    {
        uint8 * ptr = buf + 12;
        // Here is where we start reading numbers 12 bits at a time.
        // (What the hell do they have against 16 bits?!?)
        uint16 word = unpack_big_uint16(ptr);
        ptr += 2;
        int num_matches = word >> 4;
        trace_printf("num_matches: 0x%03X\n", num_matches);
/*      int num_bits = 4, bits = word & 0xf;
        for(int i = 0; i < num_matches; i++)
        {
            while(num_bits < 12)
            {
                bits = (bits << 8) | *ptr++;
                num_bits += 8;
            }
            trace_printf("match #%d: 0x%03X\n",
                i, (bits >> (num_bits - 12)) & 0xfff);
            num_bits -= 12;
        }*/
        int total_bits = num_matches * 12 - 4;
        ptr += total_bits / 8;
        if(total_bits % 8)
            ptr++;
        trace_printf("text: %s\n", ptr);
        if(*ptr == COMMAND_PREFIX)
        {
            do_command(reinterpret_cast<char *>(ptr + 1));
            resend = false;
        }
        else if(m_server != 0 && m_server->get_fixtalk())
        {
            // Extract the text and put it in a non-unicode message so that
            // POL can understand it.
            int len = buf + size - ptr;
            uint8 * newbuf = new uint8[8 + len];
            newbuf[0] = CODE_CLIENT_TALK;
            pack_big_uint16(newbuf + 1, 8 + len);
            newbuf[3] = mode & 0x3f;
            newbuf[4] = buf[4];     // 4+5: colour
            newbuf[5] = buf[5];
            newbuf[6] = buf[6];     // 6+7: font
            newbuf[7] = buf[7];
            memcpy(newbuf + 8, ptr, len);
            send_server(newbuf, 8 + len);
            delete /*[]*/ newbuf;
            resend = false;
        }
    }

    // Must have at least 3 unicode characters (prefix + 1 + null)
    else
    {
        uint16 uc = unpack_big_uint16(buf + 12);
        if(size >= 18 && uc == COMMAND_PREFIX)
        {
            int len = (size - 14) / 2;
            char * cmd = new char[len + 1];
            uint8 * ucmd = buf + 14;
            for(int i = 0; i < len; i++)
                if(ucmd[i*2] == 0)
                    cmd[i] = ucmd[i*2+1];
                else
                    cmd[i] = '?';
            cmd[len] = '\0';
            do_command(cmd);
            delete /*[]*/ cmd;
            resend = false;
        }
    }
    return resend;
}

bool Injection::handle_server_talk(uint8 * buf, int size)
{
    if(m_targeting_handler)
    {
        bool resend = m_targeting_handler->handle_server_talk(buf, size);
        if(resend) resend = m_spells->handle_server_talk(buf, size);
        if(resend) resend = m_skills->handle_server_talk(buf, size);
        return(resend);
    }
    return false;
}

bool Injection::handle_open_gump(uint8 * buf, int size)
{
//  return true;
    return m_runebook_handler->handle_runebook(buf, size);
}

////////////////////////////////////////////////////////////////////////////////
//// Command handlers:

#define COMMAND(name) { #name, &Injection::command_ ## name }

// private static
Command Injection::m_commands[] =
{
    COMMAND(fixwalk),
    COMMAND(filterweather),
    COMMAND(fixtalk),
    COMMAND(dump),
    COMMAND(flush),
    COMMAND(usetype),
    COMMAND(usefromground),
    COMMAND(useobject),
    COMMAND(waittargettype),
    COMMAND(waittargetground),
    COMMAND(waittargetobject),
    COMMAND(waittargetobjecttype),
    COMMAND(waittargetlast),
    COMMAND(waittargetself),
    COMMAND(canceltarget),
    COMMAND(setarm),
    COMMAND(unsetarm),
    COMMAND(arm),
    COMMAND(disarm),
    COMMAND(setdress),
    COMMAND(unsetdress),
    COMMAND(dress),
    COMMAND(undress),
    COMMAND(removehat),
    COMMAND(removeearrings),
    COMMAND(removeneckless),
    COMMAND(removering),
    COMMAND(dismount),
    COMMAND(mount),
    COMMAND(waitmenu),
    COMMAND(cancelmenu),
    COMMAND(buy),
    COMMAND(sell),
    COMMAND(shop),
    COMMAND(light),
    COMMAND(saveconfig),
    COMMAND(version),
    COMMAND(dye),
    COMMAND(snoop),
    COMMAND(info),
    COMMAND(hide),
    COMMAND(setreceivingcontainer),
    COMMAND(unsetreceivingcontainer),
    COMMAND(emptycontainer),
    COMMAND(grab),
    COMMAND(cast),
    COMMAND(setcatchbag),
    COMMAND(unsetcatchbag),
    COMMAND(bandageself),
    COMMAND(addrecall),
    COMMAND(addgate),
    COMMAND(setdefault),
    COMMAND(recall),
    COMMAND(gate),
    COMMAND(useskill),
    COMMAND(poison),
    COMMAND(fixhotkeys),
};

// This function is based on stringtok() from the libstdc++ documentation:
void commandtok (arglist_t & args, string const & in)
{
    const string::size_type len = in.length();
          string::size_type i = 0;

    while(i < len)
    {
        // eat leading whitespace
        i = in.find_first_not_of (' ', i);
        if(i == string::npos)
            return;   // nothing left but white space

        // find the end of the token
        string::size_type j;
        if(in[i] == '\'')
        {
            i++;
            if(i == len)
                return;
            j = in.find_first_of('\'', i);
        }
        else
            j = in.find_first_of(' ', i);

        // push token
        if(j == string::npos)
        {
            args.push_back(in.substr(i));
            return;
        }
        else
            args.push_back(in.substr(i, j - i));

        // set up for next loop
        i = j + 1;
    }
}

void Injection::do_command(const char * cmd)
{
    if(m_world == 0)
        return;
    arglist_t words;
    // Split the command into words
    commandtok(words, cmd);
    if(words.size() == 0)
    {
        client_print("Error: empty command");
        return;
    }
    const string & cmdname = words[0];

    for(arglist_t::size_type i = 0; i < words.size(); i++)
        trace_printf("words[%u]: %s\n", i, words[i].c_str());
    // Search for the command in the table.
    // TODO: use a hash_map<> to avoid linear search?
    {for(unsigned i = 0; i < sizeof(m_commands) / sizeof(m_commands[0]); i++)
        if(cmdname == m_commands[i].name)
        {
            (this ->* (m_commands[i].handler))(words);
            return;
    }}
    if(!HandleCommandInDll(cmd))
        client_print(string("Unknown command: ") + cmdname);
}

void Injection::command_fixwalk(const arglist_t & /*args*/)
{
    if(m_server == 0)
        return;
    bool fixwalk = m_server->get_fixwalk();
    m_server->set_fixwalk(!fixwalk);
    if(fixwalk)
        client_print("fixwalk is now off");
    else
        client_print("fixwalk is now on");
}

void Injection::command_filterweather(const arglist_t & /*args*/)
{
    if(m_server == 0)
        return;
    bool filter_weather = m_server->get_filter_weather();
    m_server->set_filter_weather(!filter_weather);
    if(filter_weather)
        client_print("weather is now on");
    else
        client_print("weather is now off");
}

void Injection::command_fixtalk(const arglist_t & /*args*/)
{
    if(m_server == 0)
        return;
    bool fixtalk = m_server->get_fixtalk();
    m_server->set_fixtalk(!fixtalk);
    if(fixtalk)
        client_print("fixtalk is now off");
    else
        client_print("fixtalk is now on");
}

void Injection::command_dump(const arglist_t & /*args*/)
{
    dump_world();
}

void Injection::command_flush(const arglist_t & /*args*/)
{
    log_flush();
    client_print("Log flushed.");
}

void Injection::command_usetype(const arglist_t & args)
{
    if(args.size() < 2 || args.size() > 3)
    {
        client_print("Usage: usetype (type) [color]");
        client_print("Where type is a registered name or a graphic 0x....");
        client_print("and optionally color is the hex value of the color ex: 0x....");
        return;
    }
    else
    {
        if(args.size() == 2)
            use(args[1]);
        else
            use(args[1], args[2]);
    }
}

void Injection::command_usefromground(const arglist_t & args)
{
    if(args.size() < 2 || args.size() > 3)
    {
        client_print("Usage: usefromground (type) [color]");
        client_print("Where type is a registered name or a graphic 0x....");
        client_print("and optionally color is the hex value of the color ex: 0x....");
        return;
    }
    else
    {
        if(args.size() == 2)
            useground(args[1]);
        else
            useground(args[1], args[2]);
    }
}

void Injection::command_useobject(const arglist_t & args)
{
    if(args.size() != 2)
    {
        client_print("Usage: useobject (object)");
        client_print("Where object is a registered object name or serial 0x........");
        return;
    }
    else
    {
        useobject(args[1]);
    }
}

void Injection::use(const string & name)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        uint8 buf[5];
        buf[0] = CODE_DOUBLE_CLICK_ITEM;
        pack_big_uint32(buf + 1, obj->get_serial());
        send_server(buf, 5);
    }
}

void Injection::use(const string & name, const string & color)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid color index");
            return;
        }
        x_color = l;
    }
    else
    {
        client_print("Invalid color index");
        return;
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic, x_color);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        uint8 buf[5];
        buf[0] = CODE_DOUBLE_CLICK_ITEM;
        pack_big_uint32(buf + 1, obj->get_serial());
        send_server(buf, 5);
    }
}

void Injection::useground(const string & name)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }

    GameObject * obj = m_world->find_world_graphic(graphic,USE_DISTANCE);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        uint8 buf[5];
        buf[0] = CODE_DOUBLE_CLICK_ITEM;
        pack_big_uint32(buf + 1, obj->get_serial());
        send_server(buf, 5);
    }
}

void Injection::useground(const string & name, const string & color)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid color index");
            return;
        }
        x_color = l;
    }
    else
    {
        client_print("Invalid color index");
        return;
    }

    GameObject * obj = m_world->find_world_graphic(graphic, x_color, USE_DISTANCE);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        uint8 buf[5];
        buf[0] = CODE_DOUBLE_CLICK_ITEM;
        pack_big_uint32(buf + 1, obj->get_serial());
        send_server(buf, 5);
    }
}

void Injection::useobject(const string & name)
{
    if(m_world == 0)
        return;
    uint32 serial;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        string_to_serial(name.c_str(), serial);
        if(serial > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial = m_character->find_obj(name);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    trace_printf("Using item 0x%08lX\n", serial);
    uint8 buf[5];
    buf[0] = CODE_DOUBLE_CLICK_ITEM;
    pack_big_uint32(buf + 1, serial);
    send_server(buf, 5);
}

void Injection::command_waittargetground(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if(args.size() < 2 || args.size() > 3)
    {
        client_print("Usage: waittargetground (type) [color]");
        client_print("Where type is a registered name or a graphic 0x....");
        client_print("and optionally color is the hex value of the color ex: 0x....");
        return;
    }
    else
    {
        if(args.size() == 2)
            targetgroundtype(args[1].c_str());
        else
            targetgroundtype(args[1].c_str(), args[2].c_str());
    }

}

void Injection::command_waittargettype(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if(args.size() < 2 || args.size() > 3)
    {
        client_print("Usage: waittargettype (type) [color]");
        client_print("Where type is a registered name or a graphic 0x....");
        client_print("and optionally color is the hex value of the color ex: 0x....");
        return;
    }
    else
    {
        if(args.size() == 2)
            targettype(args[1].c_str());
        else
            targettype(args[1].c_str(), args[2].c_str());
    }

}

void Injection::command_waittargetobject(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if((args.size() < 2) || (args.size() > 3))
    {
        client_print("Usage: waittargetobject (object) [object2]");
        client_print("Where object is a registered object name or serial 0x........");
        client_print("Where object2 is optionally a registered object name or serial 0x........");
        return;
    }
    else
    {
        if(args.size() == 2)
            targetobject(args[1]);
        else
            targetobject(args[1], args[2]);
    }
}

void Injection::command_waittargetobjecttype(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if((args.size() < 3) || (args.size() > 4))
    {
        client_print("Usage: waittargetobjecttype (target) (target2) [color]");
        client_print("Where target is a registered object name or serial 0x........");
        client_print("Where target2 is a registered object type name or serial 0x....");
        client_print("Where color is an optional color index 0x....");
        return;
    }
    else
    {
        if(args.size() == 3)
            targetobjecttype(args[1], args[2]);
        else
            targetobjecttype(args[1], args[2], args[3]);
    }
}

void Injection::command_waittargetlast(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if(args.size() != 1)
    {
        client_print("Usage: waittargetlast");
        return;
    }
    else if(!m_last_target_set)
    {
        client_print("No last target available");
        return;
    }
    else
    {
        m_targeting_handler->wait_target(m_last_target);
    }
}

void Injection::command_waittargetself(const arglist_t & args)
{
    if(m_targeting_handler == 0)
        return;
    if(args.size() != 1)
    {
        client_print("Usage: waittargetself");
        return;
    }
    else
    {
        m_targeting_handler->wait_target(m_world->get_player()->get_serial());
    }
}

void Injection::command_canceltarget(const arglist_t & /*args*/)
{
    if(m_targeting_handler == 0)
        return;
    m_targeting_handler->cancel_target();
}

void Injection::targettype(const string & name)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic);
    if(obj == 0)
    {
        client_print("No item found. Next target request will be canceled");
        m_targeting_handler->wait_target(0x0);
    }
    else
    {
        trace_printf("Targeting item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(obj->get_serial());
    }
}

void Injection::targettype(const string & name, const string & color)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid color index");
            return;
        }
        x_color = l;
    }
    else
    {
        client_print("Invalid color index");
        return;
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic, x_color);
    if(obj == 0)
    {
        client_print("No item found. Next target request will be canceled");
        m_targeting_handler->wait_target(0x0);
    }
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(obj->get_serial());
    }
}

void Injection::targetgroundtype(const string & name)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }

    GameObject * obj = m_world->find_world_graphic(graphic,USE_DISTANCE);
    if(obj == 0)
    {
        client_print("No item found. Next target request will be canceled");
        m_targeting_handler->wait_target(0x0);
    }
    else
    {
        trace_printf("Targeting item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(obj->get_serial());
    }
}

void Injection::targetgroundtype(const string & name, const string & color)
{
    if(m_world == 0)
        return;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid color index");
            return;
        }
        x_color = l;
    }
    else
    {
        client_print("Invalid color index");
        return;
    }

    GameObject * obj = m_world->find_world_graphic(graphic, x_color, USE_DISTANCE);
    if(obj == 0)
    {
        client_print("No item found. Next target request will be canceled");
        m_targeting_handler->wait_target(0x0);
    }
    else
    {
        trace_printf("Using item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(obj->get_serial());
    }
}

void Injection::targetobject(const string & name)
{
    if(m_world == 0)
        return;
    uint32 serial;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        string_to_serial(name.c_str(), serial);
        if(serial > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial = m_character->find_obj(name);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    trace_printf("Targeting object 0x%08lX\n", serial);
    m_targeting_handler->wait_target(serial);
}

void Injection::targetobject(const string & name, const string & name2)
{
    if(m_world == 0)
        return;
    uint32 serial;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        string_to_serial(name.c_str(), serial);
        if(serial > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial = m_character->find_obj(name);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    uint32 serial2;
    if(name2.length() > 2 && name2[0] == '0' && name2[1] == 'x')
    {
        string_to_serial(name2.c_str(), serial2);
        if(serial2 > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial2 = m_character->find_obj(name2);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    trace_printf("Targeting object 0x%08lX\n", serial);
    m_targeting_handler->wait_target(serial, serial2);
}

void Injection::targetobjecttype(const string & name, const string & name2)
{
    if(m_world == 0)
        return;
    uint32 serial;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        string_to_serial(name.c_str(), serial);
        if(serial > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial = m_character->find_obj(name);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    uint16 graphic;
    if(name2.length() > 2 && name2[0] == '0' && name2[1] == 'x')
    {
        long l = strtol(name2.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name2))
            graphic = m_config.find_use(name2);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Targeting item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(serial, obj->get_serial());
    }
}

void Injection::targetobjecttype(const string & name, const string & name2, const string & color)
{
    if(m_world == 0)
        return;
    uint32 serial;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        string_to_serial(name.c_str(), serial);
        if(serial > 0xffffffff)
        {
            client_print("Invalid serial index");
            return;
        }
    }
    else
    {
        if(m_character->obj_exists(name))
            serial = m_character->find_obj(name);
        else
        {
            client_print("Object name unknown");
            return;
        }
    }

    uint16 graphic;
    if(name2.length() > 2 && name2[0] == '0' && name2[1] == 'x')
    {
        long l = strtol(name2.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid graphic index");
            return;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name2))
            graphic = m_config.find_use(name2);
        else
        {
            client_print("Graphic name unknown");
            return;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            client_print("Invalid color index");
            return;
        }
        x_color = l;
    }
    else
    {
        client_print("Invalid color index");
        return;
    }

    GameObject * obj = m_world->find_inventory_graphic(graphic, x_color);
    if(obj == 0)
        client_print("No item found.");
    else
    {
        trace_printf("Targeting item 0x%08lX\n", obj->get_serial());
        m_targeting_handler->wait_target(serial, obj->get_serial());
    }
}

void Injection::command_setarm(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: setarm (identifier)");
        else
            m_dress_handler->setarm(args[1].c_str());
    }
}

void Injection::command_unsetarm(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: unsetarm (identifier)");
        else
            m_dress_handler->unsetarm(args[1].c_str());
    }
}

void Injection::command_arm(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: arm (identifier)");
        else
            m_dress_handler->arm(args[1].c_str());
    }
}

void Injection::command_disarm(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->disarm();
}

void Injection::command_setdress(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: setdress (identifier)");
        else
            m_dress_handler->set(args[1].c_str());
    }
}

void Injection::command_unsetdress(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: unsetdress (identifier)");
        else
            m_dress_handler->unset(args[1].c_str());
    }
}

void Injection::command_dress(const arglist_t & args)
{
    if(m_dress_handler != 0)
    {
        if(args.size() != 2)
            client_print("usage: dress (identifier)");
        else
            m_dress_handler->dress(args[1].c_str());
    }
}

void Injection::command_undress(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->undress();
}

void Injection::command_removehat(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->removehat();
}

void Injection::command_removeearrings(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->removeearrings();
}

void Injection::command_removeneckless(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->removeneckless();
}

void Injection::command_removering(const arglist_t & /*args*/)
{
    if(m_dress_handler != 0)
        m_dress_handler->removering();
}

void Injection::command_dismount(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    GameObject * player = m_world->get_player();
    GameObject * mount = player->find_layer(LAYER_MOUNT);
    if(mount == 0)
        client_print("You are not on a mount.");
    else
    {
        client_print("Moving mount to backpack.");
        move_backpack(mount->get_serial());
    }
}

void Injection::command_mount(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    GameObject * player = m_world->get_player();
    GameObject * mount = player->find_layer(LAYER_MOUNT);
    if(mount == 0)
    {
        client_print("Target a mount item.");
        request_target(&Injection::target_mount);
    }
    else
        client_print("You are already on a mount.");
}

void Injection::target_mount(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled mount targetting.");
    else
    {
        client_print("Equipping object.");
        move_equip(obj->get_serial(), LAYER_MOUNT);
    }
}

void Injection::command_waitmenu(const arglist_t & args)
{
    if(m_menu_handler == 0)
        return;
    if(!((args.size() == 3) || (args.size() == 5) || (args.size() == 7)))
    {
        client_print("Usage: waitmenu 'prompt' 'choice' ['prompt2' 'choice2' ['prompt3' 'choice3'] ]");
        return;
    }
    if(args.size() == 3)
        m_menu_handler->wait_menu(args[1].c_str(), args[2].c_str());
    else if(args.size() == 5)
        m_menu_handler->wait_menu(args[1].c_str(), args[2].c_str(), args[3].c_str(), args[4].c_str());
    else if(args.size() == 7)
        m_menu_handler->wait_menu(args[1].c_str(), args[2].c_str(), args[3].c_str(), args[4].c_str(), args[5].c_str(), args[6].c_str());
}

void Injection::command_cancelmenu(const arglist_t & /*args*/)
{
    if(m_menu_handler == 0)
        return;
    m_menu_handler->cancel_menu();
}

void Injection::command_buy(const arglist_t & args)
{
    if(m_vendor_handler == 0)
        return;
    if(args.size() < 2 || args.size() > 3)
        client_print("usage: buy (shopping list name) [vendor name]");
    else
    {
        if(args.size() == 2)
            m_vendor_handler->buy(args[1], "");
        else    // vendor name specified
            m_vendor_handler->buy(args[1], args[2]);
    }
}

void Injection::command_sell(const arglist_t & args)
{
    if(m_vendor_handler == 0)
        return;
    if(args.size() < 2 || args.size() > 3)
        client_print("usage: sell (shopping list name) [vendor name]");
    else
    {
        if(args.size() == 2)
            m_vendor_handler->sell(args[1], "");
        else    // vendor name specified
            m_vendor_handler->sell(args[1], args[2]);
    }
}

void Injection::command_shop(const arglist_t & /*args*/)
{
    shop();
}

void Injection::command_light(const arglist_t & args)
{
    if(m_character == 0)
        return;
    if(args.size() > 2)
        client_print("usage: light [amount]");
    else
    {
        int amount, light = m_character->get_light();

        if(args.size() == 1) // no parameters: toggle between normal/bright
        {
            if(light == LIGHT_NORMAL)   // normal, so set to brightest
                light = amount = 0;
            else    // already overridden, so return to normal
            {
                light = LIGHT_NORMAL;
                amount = m_normal_light;
            }
        }
        else
        {
            char * end;
            amount = strtol(args[1].c_str(), &end, 10);
            if(end != args[1].c_str() + args[1].length() ||
                    amount < 0 || amount > 31)
            {
                client_print("Light amount must be 0 to 31");
                return;
            }
            light = amount;
        }
        if(light == LIGHT_NORMAL)
            client_print("Light level returned to normal");
        else
            client_print("Light level permanently fixed");
        m_character->set_light(light);
        uint8 buf[2];
        buf[0] = CODE_GLOBAL_LIGHT_LEVEL;
        buf[1] = amount;
        send_client(buf, sizeof(buf));
        trace_printf("Global light level set to %d\n", amount);
    }
}

void Injection::command_saveconfig(const arglist_t & /*args*/)
{
    save_config();
}

void Injection::command_version(const arglist_t & /*args*/)
{
    client_print("Injection version: " VERSION_STRING);
}

void Injection::command_dye(const arglist_t & args)
{
    if(args.size() != 2)
    {
        client_print("usage: dye (colour number)");
        return;
    }
    int colour;
    if(!string_to_int(args[1].c_str(), colour) || colour < 0 ||
            colour >= 0xffff)
    {
        client_print("Invalid colour number");
        return;
    }
    client_print("Waiting for dye window...");
    m_dye_colour = colour;
}

void Injection::command_snoop(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    client_print("Target a container.");
    request_target(&Injection::target_snoop);
}

uint16 gump_from_graphic(uint16 graphic)
{
    uint16 gump;

    switch(graphic)
    {
    case 0x0e75:      // backpack
    case 0x0e79:      // box/pouch
        gump = 0x3C;
        break;
    case 0x0e76:      // leather bag
        gump = 0x3D;
        break;
    case 0x0e77:      // barrel
    case 0x0e7F:      // keg
        gump = 0x3E;
        break;
    case 0x0e7A:      // square basket
        gump = 0x3F;
        break;
    case 0x0e40:      // metal & gold chest
    case 0x0e41:      // metal & gold chest
        gump = 0x42;
        break;
    case 0x0e7D:      // wooden box
        gump = 0x43;
        break;
    case 0x0e3C:      // large wooden crate
    case 0x0e3D:      // large wooden crate
    case 0x0e3E:      // small wooden create
    case 0x0e3F:      // small wooden crate
    case 0x0e7E:      // wooden crate
        gump = 0x44;
        break;
    case 0x0e42:      // wooden & gold chest
    case 0x0e43:      // wooden & gold chest
        gump = 0x49;
        break;
    case 0x0e7C:      // silver chest
        gump = 0x4A;
        break;
    case 0x0e80:      // brass box
        gump = 0x4B;
        break;
    case 0x0e83:
        gump = 0x3E;
    case 0x09aa:      // wooden box
        gump=0x43;
        break;
    case 0x09A8:      // metal box
        gump=0x4B;  // fix from Daemar
        break;
    case 0x0990:      // round basket
        gump=0x41;
        break;
    case 0x09A9:      // small wooden crate
        gump=0x44;
        break;
    case 0x09AB:      // metal & silver chest
        gump=0x4A;
        break;
    case 0x09AC:
    case 0x09B1:
        gump = 0x41;
        break;
    case 0x09B0:
        gump = 0x3C;
        break;
    case 0x09B2:      // bank box (..OR.. backpack 2)
        gump = 0x4A;
        break;
    case 0x0a30:   // chest of drawers (fancy)
    case 0x0a38:   // chest of drawers (fancy)
        gump=0x48;
        break;

    case 0x0a4C:   // fancy armoire (open)
    case 0x0a4D:   // fancy armoire
    case 0x0a50:   // fancy armoire (open)
    case 0x0a51:   // fancy armoire
        gump=0x4E;
        break;

    case 0x0a4E:   // wooden armoire (open)
    case 0x0a4F:   // wooden armoire
    case 0x0a52:   // wooden armoire (open)
    case 0x0a53:   // wooden armoire
        gump=0x4F;
        break;

    case 0x0a97:   // bookcase
    case 0x0a98:   // bookcase
    case 0x0a99:   // bookcase
    case 0x0a9A:   // bookcase
    case 0x0a9B:   // bookcase
    case 0x0a9C:   // bookcase
    case 0x0a9D:    // bookcase (empty)
    case 0x0a9E:    // bookcase (empty)
        gump=0x4D;
        break;

    case 0x0a2C:   // chest of drawers (wood)
    case 0x0a34:   // chest of drawers (wood)
    case 0x0a35:   // dresser
    case 0x0a3C:   // dresser
    case 0x0a3D:   // dresser
    case 0x0a44:   // dresser
        gump=0x51;
        break;
    case 0x2006:      // coffin
        gump=0x09;
        break;
    case 0x0Fae:    // barrel with lids
        gump = 0x3E;
        break;

    case 0x1Ad7:    // potion kegs
        gump = 0x3E;
        break;

    case 0x1940:    // barrel with lids
        gump = 0x3E;
        break;
    default:
        gump = 0x3C;    // backpack
        break;
    }
    return gump;
}

void Injection::target_snoop(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled snoop targetting.");
    else
    {
        client_print("Opening container.");
        trace_printf("snooping serial: 0x%08lX, graphic: 0x%04X\n",
            obj->get_serial(), obj->get_graphic());

        // Send an Open Container message to the client
        uint8 buf[7];
        buf[0] = 0x24;  // Open container
        pack_big_uint32(buf + 1, obj->get_serial());
        pack_big_uint16(buf + 5, gump_from_graphic(obj->get_graphic()));
        send_client(buf, sizeof(buf));
    }
}

void Injection::command_info(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    client_print("Target an object for information.");
    request_target(&Injection::target_info);
}

void Injection::target_info(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled info targetting.");
    else
    {
        char desc[100];
        sprintf(desc, "Serial: 0x%08lX  Graphic: 0x%04X  Quantity: %d  Colour: 0x%04X",
            obj->get_serial(), obj->get_graphic(), obj->m_quantity,
            obj->m_colour);
        client_print(desc);
    }
}

void Injection::command_hide(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    client_print("Target an object to hide.");
    request_target(&Injection::target_hide);
}

void Injection::target_hide(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled 'hide' targetting.");
    else
    {
        uint8 buf[5];
        buf[0] = 0x1d;  // Delete Object
        pack_big_uint32(buf + 1, obj->get_serial());
        send_client(buf, sizeof(buf));
    }
}


void Injection::command_setreceivingcontainer(const arglist_t & args)
{
    if(m_world == 0)
        return;
    else
    {
        if(args.size() > 2)
        {
            client_print("Usage: setreceivingcontainer [object]");
            return;
        }
        else
        {
            if(args.size() == 1)
            {
                client_print("Target container to place items into from the emptycontainer command.");
                request_target(&Injection::target_setreceivingcontainer);
                return;
            }
            else
            {
                uint32 serial;
                if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
                {
                    string_to_serial(args[1].c_str(), serial);
                    if(serial > 0xffffffff)
                    {
                        client_print("Invalid serial index");
                        return;
                    }
                }
                else
                {
                    if(m_character->obj_exists(args[1]))
                        serial = m_character->find_obj(args[1]);
                    else
                    {
                        client_print("Object name unknown");
                        return;
                    }
                }
                set_receivingcontainer(serial);
            }
        }
    }
}

void Injection::target_setreceivingcontainer(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled 'settargetcontainer' targetting.");
    else
    {
        set_receivingcontainer(obj->get_serial());
    }
}

void Injection::set_receivingcontainer(uint32 serial)
{
    m_receiving_container = serial;
    char buf[40];
    sprintf(buf, "Receiving container set: 0x%08lX", m_receiving_container);
    client_print(buf);
}

void Injection::command_unsetreceivingcontainer(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    else
    {
        // clear selected container.
        // this should make the player backpack the default receiving container.
        m_receiving_container = 0;
        char buf[40];
        sprintf(buf, "Receiving container unset: 0x%08lX", m_receiving_container);
        client_print(buf);
    }
}

void Injection::command_emptycontainer(const arglist_t & args)
{
    if(m_world == 0)
        return;
    else
    {
        if(args.size() != 2)
        {
            client_print("usage: emptycontainer (pause in milisecs)");
            client_print("example: 'emptycontainer 500'   pauses 1/2 sec between moves");
        }
        else
        {
            empty_speed = strtol(args[1].c_str(),NULL, 16);
            client_print("Target container to empty.");
            request_target(&Injection::target_emptycontainer);
        }
    }
}

void Injection::target_emptycontainer(GameObject * obj)
{
    if(obj == 0)
        return;
    else
    {
        uint32 to_container;
        if(m_receiving_container == 0)
        {
            to_container = m_world->get_player()->get_serial();
        }
        else
        {
            to_container = m_receiving_container;
        }
        // find our from container
        GameObject * from_obj = m_world->find_object(obj->get_serial());

        for(GameObject::iterator i = from_obj->begin(); i != from_obj->end(); ++i)
        {
            uint32 item = i->get_serial();
            uint16 quantity = i->get_quantity();
            if(quantity == 0) quantity = 1;
            if(item != to_container) move_container(item, quantity, to_container);
            if(empty_speed >0)
                Sleep(empty_speed);
        }

    }
}

void Injection::command_grab(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    else
    {
        client_print("Target object to grab.");
        request_target(&Injection::target_grab);
    }
}

void Injection::target_grab(GameObject * obj)
{
    if(obj == 0)
        return;
    else
    {
        uint32 to_container;
        if(m_receiving_container == 0)
        {
            to_container = m_world->get_player()->get_serial();
        }
        else
        {
            to_container = m_receiving_container;
        }
        uint32 item = obj->get_serial();
        uint16 quantity = obj->get_quantity();
        if (quantity == 0) quantity = 1;
        move_container(item, quantity, to_container);
    }
}

void Injection::command_cast(const arglist_t & args)
{
    uint32 serial;
    if(args.size() < 2  || args.size() > 3 )
    {
        client_print("Usage: cast (spell name) [last or self or object]");
        return;
    }
    else
    {
        if(args.size() == 2)
        {
            m_spells->cast(args[1]);
            return;
        }
        else
        {
            if(args[2].length() > 2 && args[2][0] == '0' && args[2][1] == 'x')
            {
                string_to_serial(args[2].c_str(), serial);
                if(serial > 0xffffffff)
                {
                    client_print("Invalid serial index");
                    return;
                }
            }
            else
            {
                if(m_character->obj_exists(args[2]))
                    serial = m_character->find_obj(args[2]);
                else
                {
                    if(strcmp(args[2].c_str(), "last") == 0)
                    {
                        if(m_last_target_set)
                            serial = m_last_target;
                        else
                        {
                            client_print("No Last Target available");
                            return;
                        }
                    }
                    else if(strcmp(args[2].c_str(), "self") == 0)
                    {
                        serial = m_world->get_player()->get_serial();
                    }
                    else
                    {
                        client_print("Object name unknown");
                        return;
                    }
                }
            }
            m_spells->cast(args[1], serial);
            return;
        }
    }
}

void Injection::command_setcatchbag(const arglist_t & args)
{
    if(m_world == 0)
        return;
    else
    {
        if(args.size() > 2)
        {
            client_print("Usage: setcatchbag [object]");
            return;
        }
        else
        {
            if(args.size() == 1)
            {
                client_print("Target container to catch items that fall into backpack.");
                request_target(&Injection::target_setcatchbag);
                return;
            }
            else
            {
                uint32 serial;
                if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
                {
                    string_to_serial(args[1].c_str(), serial);
                    if(serial > 0xffffffff)
                    {
                        client_print("Invalid serial index");
                        return;
                    }
                }
                else
                {
                    if(m_character->obj_exists(args[1]))
                        serial = m_character->find_obj(args[1]);
                    else
                    {
                        client_print("Object name unknown");
                        return;
                    }
                }
                set_catchbag(serial);
            }
        }
    }
}

void Injection::target_setcatchbag(GameObject * obj)
{
    if(obj == 0)
        client_print("Cancelled 'setcatchbag' targetting.");
    else
    {
        set_catchbag(obj->get_serial());
    }
}

void Injection::set_catchbag(uint32 serial)
{
    m_catchbag = serial;
    m_catchbag_set = true;
    if(!m_backpack_set) set_backpack();
    char buf[40];
    sprintf(buf, "Catchbag set: 0x%08lX", m_catchbag);
    client_print(buf);
}

void Injection::command_unsetcatchbag(const arglist_t & /*args*/)
{
    if(m_world == 0)
        return;
    else
    {
        // clear selected container.
        m_catchbag = 0;
        m_catchbag_set = false;
        client_print("Catchbag unset.");
    }
}

void Injection::set_backpack()
{
    if(m_world == 0)
        return;
    m_backpack_set = false;
    GameObject * player = m_world->get_player();
    for(GameObject::iterator i = player->begin(); i != player->end(); ++i)
    {
        if(i->get_layer() == 21)
        {
            m_backpack = i->get_serial();
            m_backpack_set = true;
        }
    }

}

void Injection::command_bandageself(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 1 )
    {
        client_print("Usage: bandageself");
        return;
    }
    else
    {
        GameObject * obj = m_world->find_inventory_graphic(0x0e21);
        if(obj == 0)
        {
            client_print("No bandages found.");
            return;
        }
        m_targeting_handler->wait_target(m_world->get_player()->get_serial());
        use("0x0e21");
    }
}

void Injection::command_addrecall(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 2 )
    {
        client_print("Usage: addrecall (runbook serial)");
        return;
    }
    else
    {
        uint32 serial;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }

        GameObject * obj = m_world->find_inventory_graphic(0x1f4c);
        if(obj == 0)
        {
            client_print("No recall scrolls found.");
            return;
        }
        m_runebook_handler->add_recall(serial, obj->get_serial());
    }
}

void Injection::command_addgate(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 2 )
    {
        client_print("Usage: addgate (runbook serial)");
        return;
    }
    else
    {
        uint32 serial;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }

        GameObject * obj = m_world->find_inventory_graphic(0x1f60);
        if(obj == 0)
        {
            client_print("No gate scrolls found.");
            return;
        }
        m_runebook_handler->add_gate(serial, obj->get_serial());
    }
}

void Injection::command_setdefault(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 3 )
    {
        client_print("Usage: setdefault (runbook serial) (rune number)");
        return;
    }
    else
    {
        uint32 serial;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }
        int rune = atoi(args[2].c_str());
        m_runebook_handler->set_default(serial, rune);
    }
}

void Injection::command_recall(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 3 )
    {
        client_print("Usage: recall (runbook serial) (rune number)");
        return;
    }
    else
    {
        uint32 serial;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }
        int rune = atoi(args[2].c_str());
        m_runebook_handler->recall(serial, rune);
    }
}

void Injection::command_gate(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 3 )
    {
        client_print("Usage: gate (runbook serial) (rune number)");
        return;
    }
    else
    {
        uint32 serial;
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }
        int rune = atoi(args[2].c_str());
        m_runebook_handler->gate(serial, rune);
    }
}

void Injection::command_useskill(const arglist_t & args)
{
    uint32 serial;
    if(args.size() < 2  || args.size() > 4 )
    {
        client_print("Usage: useskill (skill name) [last or self or object]");
        return;
    }
    else
    {
        if(args.size() == 2)
        {
            m_skills->use(args[1]);
            return;
        }
        else if(args.size() == 3)
        {
            if(args[2].length() > 2 && args[2][0] == '0' && args[2][1] == 'x')
            {
                string_to_serial(args[2].c_str(), serial);
                if(serial > 0xffffffff)
                {
                    client_print("Invalid serial index");
                    return;
                }
            }
            else
            {
                if(m_character->obj_exists(args[2]))
                    serial = m_character->find_obj(args[2]);
                else
                {
                    if(strcmp(args[2].c_str(), "last") == 0)
                    {
                        if(m_last_target_set)
                            serial = m_last_target;
                        else
                        {
                            client_print("No Last Target available");
                            return;
                        }
                    }
                    else if(strcmp(args[2].c_str(), "self") == 0)
                    {
                        serial = m_world->get_player()->get_serial();
                    }
                    else
                    {
                        client_print("Object name unknown");
                        return;
                    }
                }
            }
            m_skills->use(args[1], serial);
            return;
        }
    }
}

void Injection::command_poison(const arglist_t & args)
{
    uint32 serial;
    uint32 serial2;
    if(args.size() != 2)
    {
        client_print("Usage: poison (object)");
        return;
    }
    else
    {
        if(args[1].length() > 2 && args[1][0] == '0' && args[1][1] == 'x')
        {
            string_to_serial(args[1].c_str(), serial);
            if(serial > 0xffffffff)
            {
                client_print("Invalid serial index");
                return;
            }
        }
        else
        {
            if(m_character->obj_exists(args[1]))
                serial = m_character->find_obj(args[1]);
            else
            {
                client_print("Object name unknown");
                return;
            }
        }
        // find a poison to use
        uint16 graphic;
        if(m_config.use_exists("poison"))
            graphic = m_config.find_use("poison");
        else
        {
            client_print("poison not defined in object types");
            return;
        }

        GameObject * obj = m_world->find_inventory_graphic(graphic);
        if(obj == 0)
        {
            client_print("No poison found in pack.");
            return;
        }
        else
        {
            serial2 =  obj->get_serial();
        }
        m_skills->use("Poisoning", serial, serial2);
        return;
    }
}

void Injection::command_fixhotkeys(const arglist_t & args)
{
    if(m_world == 0)
        return;
    if(args.size() != 1 )
    {
        client_print("Usage: fixhotkeys");
        return;
    }
    else
    {
        if(m_hotkeyhook !=0)
            m_hotkeyhook->remove_hook();
        m_hotkeyhook = new HotkeyHook(*this, m_character->m_hotkeys);
        if(m_hotkeyhook != 0)
            if(!m_hotkeyhook->install_hook(g_hinstance, 0))
                trace_printf("Hotkey Hook not installed");
    }
}

bool Injection::get_use_target(UseTabDialog * dialog,
    use_target_handler_t handler)
{
    if(m_world == 0)
        return false;
    if(m_targeting)
        got_target(0);
    m_targeting = true;
    m_target_handler = 0;
    m_use_tab_dialog = dialog;
    m_use_target_handler = handler;
    send_target_request();
    client_print("Select a target to find out the graphic.");
    return true;
}

bool Injection::get_object_target(ObjectTabDialog * dialog,
    object_target_handler_t handler)
{
    if(m_world == 0)
        return false;
    if(m_targeting)
        got_target(0);
    m_targeting = true;
    m_target_handler = 0;
    m_object_tab_dialog = dialog;
    m_object_target_handler = handler;
    send_target_request();
    client_print("Select a target to find out the serial.");
    return true;
}

////////////////////////////////////////////////////////////////////////////////
////

int Injection::count_object_type(const string & name)
{
    if(m_world == 0)
        return -3;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -2;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            return -1;
        }
    }
    return m_world->count_inventory_graphic(graphic);
}

int Injection::count_object_type(const string & name, const string & color)
{
    if(m_world == 0)
        return -3;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -2;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            return -1;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -4;
        }
        x_color = l;
    }
    else
    {
        return -4;
    }

    return m_world->count_inventory_graphic(graphic, x_color);
}

int Injection::count_on_ground(const string & name)
{
    if(m_world == 0)
        return -3;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -2;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            return -1;
        }
    }
    return m_world->count_on_ground(graphic, USE_DISTANCE);
}

int Injection::count_on_ground(const string & name, const string & color)
{
    if(m_world == 0)
        return -3;
    uint16 graphic;
    if(name.length() > 2 && name[0] == '0' && name[1] == 'x')
    {
        long l = strtol(name.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -2;
        }
        graphic = l;
    }
    else
    {
        if(m_config.use_exists(name))
            graphic = m_config.find_use(name);
        else
        {
            return -1;
        }
    }
    uint16 x_color;
    if(color.length() > 2 && color[0] == '0' && color[1] == 'x')
    {
        long l = strtol(color.c_str() + 2, NULL, 16);
        if(l < 0 || l > 0xffff)
        {
            return -4;
        }
        x_color = l;
    }
    else
    {
        return -4;
    }

    return m_world->count_on_ground(graphic, x_color, USE_DISTANCE);
}

////////////////////////////////////////////////////////////////////////////////
//// Methods of ClientInterface:

void Injection::send_server(uint8 * buf, int size)
{
    if(m_hook == 0)
        return;
    m_hook->send_server(buf, size);
    trace_printf("-------------- injection to server -------------\n");
    trace_dump(buf, size);

}

void Injection::send_client(uint8 * buf, int size)
{
    if(m_hook == 0)
        return;
    m_hook->send_client(buf, size);
    trace_printf("-------------- injection to client --------------\n");
    trace_dump(buf, size);
}

void Injection::client_print(const char * text)
{
    int size = 44 + strlen(text) + 1;
    uint8 * buf = new uint8[size];

    buf[0] = CODE_SERVER_TALK;
    pack_big_uint16(buf + 1, size);
    pack_big_uint32(buf + 3, INVALID_SERIAL);   // serial
    pack_big_uint16(buf + 7, 0);    // graphic
    buf[9] = 0;     // mode (0=normal)
    //pack_big_uint16(buf + 10, 0x03b2);    // colour (0x03b2=grey)
    pack_big_uint16(buf + 10, 0x0440);  // colour
    pack_big_uint16(buf + 12, 3);   // font
    strcpy(reinterpret_cast<char *>(buf + 14), "Injection"); // name
    strcpy(reinterpret_cast<char *>(buf + 44), text);

    send_client(buf, size);
    delete /*[]*/ buf;
}

void Injection::client_print(const string & text)
{
    client_print(text.c_str());
}

// Pick up an object and drop it onto another container
void Injection::move_container(uint32 serial, uint32 cserial)
{
    move_container(serial, 1, cserial);
}

// Pick up stack of objects and drop it another container
void Injection::move_container(uint32 serial, uint16 quantity, uint32 cserial)
{
    uint8 buf[7 + 14];

    buf[0] = CODE_PICK_UP_ITEM;
    pack_big_uint32(buf + 1, serial);
    pack_big_uint16(buf + 5, quantity); // quantity to pick up

    buf[7] = CODE_DROP_ITEM;
    pack_big_uint32(buf + 8, serial);
    pack_big_uint16(buf + 12, INVALID_XY);  // x
    pack_big_uint16(buf + 14, INVALID_XY);  // y
    buf[16] = 0;    // z
    pack_big_uint32(buf + 17, cserial);

    send_server(buf, sizeof(buf));
}

// move object quintity to player backpack
void Injection::move_backpack(uint32 serial, uint16 quantity)
{
    ASSERT(m_world != 0);
    move_container(serial, quantity, m_world->get_player()->get_serial());
}

// move 1 object to player backpack
void Injection::move_backpack(uint32 serial)
{
    ASSERT(m_world != 0);
    move_container(serial, 1, m_world->get_player()->get_serial());
}


void Injection::move_equip(uint32 serial, int layer)
{
    ASSERT(m_world != 0);
    uint8 buf[7 + 10];

    buf[0] = CODE_PICK_UP_ITEM;
    pack_big_uint32(buf + 1, serial);
    pack_big_uint16(buf + 5, 1);    // quantity to pick up

    buf[7] = CODE_CLIENT_EQUIP_ITEM;
    pack_big_uint32(buf + 8, serial);
    buf[12] = layer;
    pack_big_uint32(buf + 13, m_world->get_player()->get_serial());

    send_server(buf, sizeof(buf));
}

////////////////////////////////////////////////////////////////////////////////

void Injection::request_target(target_handler_t handler)
{
    ASSERT(handler != 0);

    if(m_targeting)
        got_target(0);
    m_targeting = true;
    m_target_handler = handler;
    m_use_tab_dialog = 0;
    m_use_target_handler = 0;
    m_object_tab_dialog = 0;
    m_object_target_handler = 0;
    send_target_request();
}

void Injection::send_target_request()
{
    ASSERT(m_world != 0);
    GameObject * player = m_world->get_player();
    ASSERT(player != 0);
    // If the client is already targeting due to a server request, tell
    // the server to cancel it.
    if(m_client_targeting)
    {
        send_server(m_cancel_target, sizeof(m_cancel_target));
        m_client_targeting = false;
    }
    uint8 buf[19];
    buf[0] = 0x6c;  // Target data
    buf[1] = 0;     // target request
    pack_big_uint32(buf + 2, player->get_serial());    // source serial
    buf[6] = 0;     // target an object (not location)
    pack_big_uint32(buf + 7, 0);    // target serial
    pack_big_uint16(buf + 11, 0);   // target x
    pack_big_uint16(buf + 13, 0);   // target y
    buf[15] = 0;    // unknown
    buf[16] = 0;    // target z
    pack_big_uint16(buf + 17, 0);   // graphic
    send_client(buf, sizeof(buf));
}

void Injection::got_target(GameObject * obj)
{
    ASSERT(m_targeting);
    if(m_target_handler != 0)
        (this ->* m_target_handler)(obj);
    else if(m_use_target_handler != 0)
        (m_use_tab_dialog ->* m_use_target_handler)(obj);
    else if(m_object_target_handler != 0)
        (m_object_tab_dialog ->* m_object_target_handler)(obj);
    else
        error_printf("INTERNAL ERROR: m_targeting but no target handler!");
    m_targeting = false;
}

////////////////////////////////////////////////////////////////////////////////
//// Methods of GUICallbackInterface:

void Injection::dump_world()
{
    if(m_world == 0)
        return;
    m_world->dump();
    log_flush();
}

void Injection::save_config()
{
    m_config.save();
    client_print("Configuration saved.");
}

void Injection::shop()
{
    if(m_vendor_handler != 0)
        m_vendor_handler->shop();
}

void Injection::update_display()
{
    m_counter_manager.update();
}

string Injection::get_version()
{
    return VERSION_STRING;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Get error text ( exported function )
//
//  PARAMETERS:
//      int error       Error number
//
//  RETURNS:
//      const char *            Error description
//
////////////////////////////////////////////////////////////////////////////////

DLLEXPORT const char * GetErrorText(int error)
{
    switch(error)
    {
    case INJECTION_ERROR_NONE:
        return "No error";
    case INJECTION_ERROR_CONFIG:
        return "Configuration file error";
    case INJECTION_ERROR_GUI:
        return "GUI initialisation error";
    case INJECTION_ERROR_MEMORY:
        return "Out of memory error";
    }

    return "Undefined error";
}



////////////////////////////////////////////////////////////////////////////////
//
// Get info text ( exported function )
//
//  PARAMETERS:
//      -none-
//
//  RETURNS:
//      char *          Module description
//
////////////////////////////////////////////////////////////////////////////////

DLLEXPORT const char * GetInfoText()
{
    return "Injection " VERSION_STRING;
}



////////////////////////////////////////////////////////////////////////////////
//
//  Install the DLL ( exported function )
//
//  PARAMETERS:
//      unsigned int checksum   Checksum of the patch target
//      unsigned int length     Length of the patch target
//
//  RETURNS:
//      int     INJECTION_ERROR_XXX:    Failed to load DLL
//              INJECTION_ERROR_NONE:   Success
//
////////////////////////////////////////////////////////////////////////////////

Injection * g_injection = 0;
HANDLE g_MainClientThread=0;

DLLEXPORT int Install(unsigned int checksum, unsigned int length)
{
	// ASSERT(g_injection == 0); - this is the reason of many injections popping up sometimes

    if(g_injection || g_MainClientThread)
    	return TRUE; 

	DuplicateHandle(GetCurrentProcess(),
		GetCurrentThread(),GetCurrentProcess(),
		&g_MainClientThread,THREAD_ALL_ACCESS,FALSE,
		0);

	CreateThread(NULL,4096,MyWatchForHangClientThread,0,0,0);

// Now all exceptions would be handled in my filter
    SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

	WORD FPUcw=0x27f;
	__asm {
		fstcw [FPUcw]
	}

    g_injection = new Injection;
	int t=g_injection->init(checksum, length);

// One of our DLLs (probably script.dll) destroys the FPU control word.
// This was one of the main crash reasons. So, restore it to the value 
// set by client.
	__asm {
		fldcw [FPUcw]
	}

	return t;
}

void Uninstall()
{
    if(g_injection != 0)
    {
        delete g_injection;
        g_injection = 0;
    }
}

