/*
Minetest-c55
Copyright (C) 2010 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*
=============================== NOTES ==============================

TODO: Move the default settings into some separate file

*/

#ifndef SERVER
	#ifdef _WIN32
	#else
		#error "For a server build, SERVER must be defined globally"
	#endif
#endif

#ifdef UNITTEST_DISABLE
	#ifdef _WIN32
		#pragma message ("Disabling unit tests")
	#else
		#warning "Disabling unit tests"
	#endif
	// Disable unit tests
	#define ENABLE_TESTS 0
#else
	// Enable unit tests
	#define ENABLE_TESTS 1
#endif

#ifdef _MSC_VER
#pragma comment(lib, "jthread.lib")
#pragma comment(lib, "zlibwapi.lib")
#endif

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#define sleep_ms(x) Sleep(x)
#else
	#include <unistd.h>
	#define sleep_ms(x) usleep(x*1000)
#endif

#include <iostream>
#include <fstream>
#include <time.h>
#include <jmutexautolock.h>
#include <locale.h>
#include "common_irrlicht.h"
#include "debug.h"
#include "map.h"
#include "player.h"
#include "main.h"
#include "test.h"
#include "environment.h"
#include "server.h"
#include "serialization.h"
#include "constants.h"
#include "strfnd.h"
#include "porting.h"

// Dummy variable
IrrlichtDevice *g_device = NULL;

/*
	Settings.
	These are loaded from the config file.
*/

Settings g_settings;

// Sets default settings
void set_default_settings()
{
	// Client stuff
	g_settings.setDefault("wanted_fps", "30");
	g_settings.setDefault("fps_max", "60");
	g_settings.setDefault("viewing_range_nodes_max", "300");
	g_settings.setDefault("viewing_range_nodes_min", "35");
	g_settings.setDefault("screenW", "");
	g_settings.setDefault("screenH", "");
	g_settings.setDefault("host_game", "");
	g_settings.setDefault("port", "");
	g_settings.setDefault("address", "");
	g_settings.setDefault("name", "");
	g_settings.setDefault("random_input", "false");
	g_settings.setDefault("client_delete_unused_sectors_timeout", "1200");
	g_settings.setDefault("enable_fog", "true");

	// Server stuff
	g_settings.setDefault("creative_mode", "false");
	g_settings.setDefault("heightmap_blocksize", "32");
	g_settings.setDefault("height_randmax", "constant 50.0");
	g_settings.setDefault("height_randfactor", "constant 0.6");
	g_settings.setDefault("height_base", "linear 0 0 0");
	g_settings.setDefault("plants_amount", "1.0");
	g_settings.setDefault("ravines_amount", "1.0");
	g_settings.setDefault("objectdata_interval", "0.2");
	g_settings.setDefault("active_object_range", "2");
	g_settings.setDefault("max_simultaneous_block_sends_per_client", "1");
	g_settings.setDefault("max_simultaneous_block_sends_server_total", "4");
	g_settings.setDefault("disable_water_climb", "true");
	g_settings.setDefault("endless_water", "true");
	g_settings.setDefault("max_block_send_distance", "5");
	g_settings.setDefault("max_block_generate_distance", "4");
}

/*
	Debug streams
*/

// Connection
std::ostream *dout_con_ptr = &dummyout;
std::ostream *derr_con_ptr = &dstream_no_stderr;

// Server
std::ostream *dout_server_ptr = &dstream;
std::ostream *derr_server_ptr = &dstream;

// Client
std::ostream *dout_client_ptr = &dstream;
std::ostream *derr_client_ptr = &dstream;


/*
	Timestamp stuff
*/

JMutex g_timestamp_mutex;

std::string getTimestamp()
{
	if(g_timestamp_mutex.IsInitialized()==false)
		return "";
	JMutexAutoLock lock(g_timestamp_mutex);
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char cs[20];
	strftime(cs, 20, "%H:%M:%S", tm);
	return cs;
}

int main(int argc, char *argv[])
{
	/*
		Low-level initialization
	*/

	bool disable_stderr = false;
#ifdef _WIN32
	disable_stderr = true;
#endif

	// Initialize debug streams
	debugstreams_init(disable_stderr, DEBUGFILE);
	// Initialize debug stacks
	debug_stacks_init();

	DSTACK(__FUNCTION_NAME);

	try
	{
	
	/*
		Parse command line
	*/
	
	// List all allowed options
	core::map<std::string, ValueSpec> allowed_options;
	allowed_options.insert("help", ValueSpec(VALUETYPE_FLAG));
	allowed_options.insert("config", ValueSpec(VALUETYPE_STRING,
			"Load configuration from specified file"));
	allowed_options.insert("port", ValueSpec(VALUETYPE_STRING));
	allowed_options.insert("disable-unittests", ValueSpec(VALUETYPE_FLAG));
	allowed_options.insert("enable-unittests", ValueSpec(VALUETYPE_FLAG));

	Settings cmd_args;
	
	bool ret = cmd_args.parseCommandLine(argc, argv, allowed_options);

	if(ret == false || cmd_args.getFlag("help"))
	{
		dstream<<"Allowed options:"<<std::endl;
		for(core::map<std::string, ValueSpec>::Iterator
				i = allowed_options.getIterator();
				i.atEnd() == false; i++)
		{
			dstream<<"  --"<<i.getNode()->getKey();
			if(i.getNode()->getValue().type == VALUETYPE_FLAG)
			{
			}
			else
			{
				dstream<<" <value>";
			}
			dstream<<std::endl;

			if(i.getNode()->getValue().help != NULL)
			{
				dstream<<"      "<<i.getNode()->getValue().help
						<<std::endl;
			}
		}

		return cmd_args.getFlag("help") ? 0 : 1;
	}


	/*
		Basic initialization
	*/

	// Initialize default settings
	set_default_settings();
	
	// Print startup message
	dstream<<DTIME<<"minetest-c55 server"
			" with SER_FMT_VER_HIGHEST="<<(int)SER_FMT_VER_HIGHEST
			<<", ENABLE_TESTS="<<ENABLE_TESTS
			<<std::endl;
	
	// Set locale. This is for forcing '.' as the decimal point.
	std::locale::global(std::locale("C"));
	// This enables printing all characters in bitmap font
	setlocale(LC_CTYPE, "en_US");

	// Initialize sockets
	sockets_init();
	atexit(sockets_cleanup);
	
	// Initialize timestamp mutex
	g_timestamp_mutex.Init();

	/*
		Initialization
	*/

	/*
		Read config file
	*/
	
	// Path of configuration file in use
	std::string configpath = "";
	
	if(cmd_args.exists("config"))
	{
		bool r = g_settings.readConfigFile(cmd_args.get("config").c_str());
		if(r == false)
		{
			dstream<<"Could not read configuration from \""
					<<cmd_args.get("config")<<"\""<<std::endl;
			return 1;
		}
		configpath = cmd_args.get("config");
	}
	else
	{
		const char *filenames[2] =
		{
			"../minetest.conf",
			"../../minetest.conf"
		};

		for(u32 i=0; i<2; i++)
		{
			bool r = g_settings.readConfigFile(filenames[i]);
			if(r)
			{
				configpath = filenames[i];
				break;
			}
		}
	}

	// Initialize random seed
	srand(time(0));

	/*
		Run unit tests
	*/
	if((ENABLE_TESTS && cmd_args.getFlag("disable-unittests") == false)
			|| cmd_args.getFlag("enable-unittests") == true)
	{
		run_tests();
	}
	
	// Read map parameters from settings

	HMParams hm_params;
	hm_params.blocksize = g_settings.getU16("heightmap_blocksize");
	hm_params.randmax = g_settings.get("height_randmax");
	hm_params.randfactor = g_settings.get("height_randfactor");
	hm_params.base = g_settings.get("height_base");

	MapParams map_params;
	map_params.plants_amount = g_settings.getFloat("plants_amount");
	map_params.ravines_amount = g_settings.getFloat("ravines_amount");

	/*
		Check parameters
	*/

	std::cout<<std::endl<<std::endl;
	
	std::cout
	<<"        .__               __                   __   "<<std::endl
	<<"  _____ |__| ____   _____/  |_  ____   _______/  |_ "<<std::endl
	<<" /     \\|  |/    \\_/ __ \\   __\\/ __ \\ /  ___/\\   __\\"<<std::endl
	<<"|  Y Y  \\  |   |  \\  ___/|  | \\  ___/ \\___ \\  |  |  "<<std::endl
	<<"|__|_|  /__|___|  /\\___  >__|  \\___  >____  > |__|  "<<std::endl
	<<"      \\/        \\/     \\/          \\/     \\/        "<<std::endl
	<<std::endl
	<<"Now with more waterish water!"
	<<std::endl;

	std::cout<<std::endl;
	
	// Port?
	u16 port = 30000;
	if(cmd_args.exists("port"))
	{
		port = cmd_args.getU16("port");
	}
	else if(g_settings.exists("port"))
	{
		port = g_settings.getU16("port");
	}
	else
	{
		dstream<<"Please specify port (in config or on command line)"
				<<std::endl;
	}
	
	DSTACK("Dedicated server branch");
	
	std::cout<<std::endl;
	std::cout<<"========================"<<std::endl;
	std::cout<<"Running dedicated server"<<std::endl;
	std::cout<<"========================"<<std::endl;
	std::cout<<std::endl;

	Server server("../map", hm_params, map_params);
	server.start(port);

	for(;;)
	{
		// This is kind of a hack but can be done like this
		// because server.step() is very light
		sleep_ms(30);
		server.step(0.030);

		static int counter = 0;
		counter--;
		if(counter <= 0)
		{
			counter = 10;

			core::list<PlayerInfo> list = server.getPlayerInfo();
			core::list<PlayerInfo>::Iterator i;
			static u32 sum_old = 0;
			u32 sum = PIChecksum(list);
			if(sum != sum_old)
			{
				std::cout<<DTIME<<"Player info:"<<std::endl;
				for(i=list.begin(); i!=list.end(); i++)
				{
					i->PrintLine(&std::cout);
				}
			}
			sum_old = sum;
		}
	}

	/*
		Update configuration file
	*/
	if(configpath != "")
	{
		g_settings.updateConfigFile(configpath.c_str());
	}

	} //try
	catch(con::PeerNotFoundException &e)
	{
		dstream<<DTIME<<"Connection timed out."<<std::endl;
	}
#if CATCH_UNHANDLED_EXCEPTIONS
	/*
		This is what has to be done in every thread to get suitable debug info
	*/
	catch(std::exception &e)
	{
		dstream<<std::endl<<DTIME<<"An unhandled exception occurred: "
				<<e.what()<<std::endl;
		assert(0);
	}
#endif

	debugstreams_deinit();
	
	return 0;
}

//END
