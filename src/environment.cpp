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

#include "environment.h"
#include "main.h" // g_device for timing debug

Environment::Environment(Map *map, std::ostream &dout):
		m_dout(dout)
{
	m_map = map;
	m_daynight_ratio = 0.2;
}

Environment::~Environment()
{
	// Deallocate players
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		delete (*i);
	}
	
	// The map is removed by the SceneManager
	m_map->drop();
	//delete m_map;
}

void Environment::step(float dtime)
{
	DSTACK(__FUNCTION_NAME);
	/*
		Run Map's timers
	*/
	//TimeTaker maptimerupdatetimer("m_map->timerUpdate()", g_device);
	// 0ms
	m_map->timerUpdate(dtime);
	//maptimerupdatetimer.stop();

	/*
		Get the highest speed some player is going
	*/
	//TimeTaker playerspeed("playerspeed", g_device);
	// 0ms
	f32 maximum_player_speed = 0.001; // just some small value
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		f32 speed = (*i)->getSpeed().getLength();
		if(speed > maximum_player_speed)
			maximum_player_speed = speed;
	}
	//playerspeed.stop();
	
	// Maximum time increment (for collision detection etc)
	// Allow 0.1 blocks per increment
	// time = distance / speed
	f32 dtime_max_increment = 0.1*BS / maximum_player_speed;
	// Maximum time increment is 10ms or lower
	if(dtime_max_increment > 0.01)
		dtime_max_increment = 0.01;
	
	//TimeTaker playerupdate("playerupdate", g_device);
	
	/*
		Stuff that has a maximum time increment
	*/
	// Don't allow overly huge dtime
	if(dtime > 0.5)
		dtime = 0.5;

	u32 loopcount = 0;
	do
	{
		loopcount++;

		f32 dtime_part;
		if(dtime > dtime_max_increment)
			dtime_part = dtime_max_increment;
		else
			dtime_part = dtime;
		dtime -= dtime_part;
		
		/*
			Handle players
		*/
		for(core::list<Player*>::Iterator i = m_players.begin();
				i != m_players.end(); i++)
		{
			Player *player = *i;
			
			// Apply physics to local player
			if(player->isLocal())
			{
				// Apply gravity to local player
				v3f speed = player->getSpeed();
				speed.Y -= 9.81 * BS * dtime_part * 2;

				/*
					Apply water resistance
				*/
				if(player->in_water)
				{
					f32 max_down = 1.0*BS;
					if(speed.Y < -max_down) speed.Y = -max_down;

					f32 max = 2.0*BS;
					if(speed.getLength() > max)
					{
						speed = speed / speed.getLength() * max;
					}
				}

				player->setSpeed(speed);
			}

			/*
				Move the player.
				For local player, this also calculates collision detection.
			*/
			player->move(dtime_part, *m_map);
			
			/*
				Add footsteps to grass
			*/
			//TimeTaker footsteptimer("footstep", g_device);
			// 0ms
			v3f playerpos = player->getPosition();
			// Get node that is at BS/4 under player
			v3s16 bottompos = floatToInt(playerpos + v3f(0,-BS/4,0));
			try{
				MapNode n = m_map->getNode(bottompos);
				if(n.d == CONTENT_GRASS)
				{
					n.d = CONTENT_GRASS_FOOTSTEPS;
					m_map->setNode(bottompos, n);
#ifndef SERVER
					// Update mesh on client
					if(m_map->mapType() == MAPTYPE_CLIENT)
					{
						v3s16 p_blocks = getNodeBlockPos(bottompos);
						MapBlock *b = m_map->getBlockNoCreate(p_blocks);
						b->updateMesh(m_daynight_ratio);
					}
#endif
				}
			}
			catch(InvalidPositionException &e)
			{
			}
			//footsteptimer.stop();
		}
	}
	while(dtime > 0.001);
	
	//std::cout<<"Looped "<<loopcount<<" times."<<std::endl;
}

Map & Environment::getMap()
{
	return *m_map;
}

void Environment::addPlayer(Player *player)
{
	DSTACK(__FUNCTION_NAME);
	//Check that only one local player exists and peer_ids are unique
#ifndef SERVER
	assert(player->isLocal() == false || getLocalPlayer() == NULL);
#endif
	assert(getPlayer(player->peer_id) == NULL);
	m_players.push_back(player);
}

void Environment::removePlayer(u16 peer_id)
{
	DSTACK(__FUNCTION_NAME);
re_search:
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		Player *player = *i;
		if(player->peer_id != peer_id)
			continue;
		
		delete player;
		m_players.erase(i);
		// See if there is an another one
		// (shouldn't be, but just to be sure)
		goto re_search;
	}
}

#ifndef SERVER
LocalPlayer * Environment::getLocalPlayer()
{
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		Player *player = *i;
		if(player->isLocal())
			return (LocalPlayer*)player;
	}
	return NULL;
}
#endif

Player * Environment::getPlayer(u16 peer_id)
{
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		Player *player = *i;
		if(player->peer_id == peer_id)
			return player;
	}
	return NULL;
}

core::list<Player*> Environment::getPlayers()
{
	return m_players;
}

void Environment::printPlayers(std::ostream &o)
{
	o<<"Players in environment:"<<std::endl;
	for(core::list<Player*>::Iterator i = m_players.begin();
			i != m_players.end(); i++)
	{
		Player *player = *i;
		o<<"Player peer_id="<<player->peer_id<<std::endl;
	}
}

#ifndef SERVER
void Environment::updateMeshes(v3s16 blockpos)
{
	m_map->updateMeshes(blockpos, m_daynight_ratio);
}

void Environment::expireMeshes(bool only_daynight_diffed)
{
	m_map->expireMeshes(only_daynight_diffed);
}
#endif

void Environment::setDayNightRatio(u32 r)
{
	m_daynight_ratio = r;
}

u32 Environment::getDayNightRatio()
{
	return m_daynight_ratio;
}

