/*
Copyright (C) 2006-2007 Benjamin Litzelmann ("Kurim")
for Chasseur de bots association.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "g_local.h"

#define EBHIT_FOR_AWARD     3
#define MULTIKILL_INTERVAL  3000
#define LB_TIMEOUT_FOR_COMBO    200
#define GUNBLADE_TIMEOUT_FOR_COMBO  400

void G_PlayerAward( const edict_t *ent, const char *awardMsg ) {
	char cmd[MAX_STRING_CHARS];

	//asdasd
	if( !awardMsg || !awardMsg[0] || !ent->r.client ) {
		return;
	}

	// Check this before announcement!
	// The same test is put in StatsowFacade::AddAward()
	// but it cuts off just saving awards in stats.
	// Having redundant checks won't harm.
	// Moreover its sufficient to check this at StatsowFacade level
	// for meta-awards as they aren't announced.

	// Update: we have decided to skip this test
	//if( ChatHandlersChain::Instance()->SkipStatsForClient( ent ) ) {
	//	return;
	//}

	Q_snprintfz( cmd, sizeof( cmd ), "aw \"%s\"", awardMsg );
	trap_GameCmd( ent, cmd );

	if( dedicated->integer ) {
		G_Printf( "%s", COM_RemoveColorTokens( va( "%s receives a '%s' award.\n", ent->r.client->netname, awardMsg ) ) );
	}

	ent->r.client->level.stats.awards++;
	teamlist[ent->s.team].stats.awards++;
	G_Gametype_ScoreEvent( ent->r.client, "award", awardMsg );

	StatsowFacade::Instance()->AddAward( ent, awardMsg );

	// add it to every player who's chasing this player
	for( edict_t *other = game.edicts + 1; PLAYERNUM( other ) < gs.maxclients; other++ ) {
		if( !other->r.client || !other->r.inuse || !other->r.client->resp.chase.active ) {
			continue;
		}

		if( other->r.client->resp.chase.target == ENTNUM( ent ) ) {
			trap_GameCmd( other, cmd );
		}
	}
}

void G_PlayerMetaAward( const edict_t *ent, const char *awardMsg ) {
	/*
	* ch : meta-award is an award that isn't announced but
	* it is sent to MM
	*/

	if( !awardMsg || !awardMsg[0] || !ent->r.client ) {
		return;
	}

	StatsowFacade::Instance()->AddMetaAward( ent, awardMsg );
}

#define COMBO_FLAG( a )   ( 1 << ( a - 1 ) )

void G_AwardPlayerHit( edict_t *targ, edict_t *attacker, int mod ) {
	int flag = -1;

	if( attacker->s.team == targ->s.team && attacker->s.team > TEAM_PLAYERS ) {
		return;
	}

	switch( mod ) {
		case MOD_INSTAGUN_W:
		case MOD_INSTAGUN_S:
			attacker->r.client->resp.awardInfo.ebhit_count++;
			if( attacker->r.client->resp.awardInfo.ebhit_count == EBHIT_FOR_AWARD ) {
				attacker->r.client->resp.awardInfo.ebhit_count = 0;
				attacker->r.client->resp.awardInfo.accuracy_award++;
				G_PlayerAward( attacker, S_COLOR_BLUE "Accuracy!" );
			}
			flag = COMBO_FLAG( WEAP_INSTAGUN );
			break;
		case MOD_ELECTROBOLT_W:
		case MOD_ELECTROBOLT_S:
			attacker->r.client->resp.awardInfo.ebhit_count++;
			if( attacker->r.client->resp.awardInfo.ebhit_count == EBHIT_FOR_AWARD ) {
				attacker->r.client->resp.awardInfo.ebhit_count = 0;
				attacker->r.client->resp.awardInfo.accuracy_award++;
				G_PlayerAward( attacker, S_COLOR_BLUE "Accuracy!" );
			}
			flag = COMBO_FLAG( WEAP_ELECTROBOLT );
			break;
		case MOD_SHOCKWAVE_W:
		case MOD_SHOCKWAVE_S:
			flag = COMBO_FLAG( WEAP_SHOCKWAVE );
			break;
		case MOD_ROCKET_W:
		case MOD_ROCKET_S:
		case MOD_ROCKET_SPLASH_W:
		case MOD_ROCKET_SPLASH_S:
			flag = COMBO_FLAG( WEAP_ROCKETLAUNCHER );
			break;
		case MOD_GUNBLADE_W:
		case MOD_GUNBLADE_S:
			flag = COMBO_FLAG( WEAP_GUNBLADE );
			break;
		case MOD_MACHINEGUN_W:
		case MOD_MACHINEGUN_S:
			flag = COMBO_FLAG( WEAP_MACHINEGUN );
			break;
		case MOD_RIOTGUN_W:
		case MOD_RIOTGUN_S:
			flag = COMBO_FLAG( WEAP_RIOTGUN );
			break;
		case MOD_GRENADE_W:
		case MOD_GRENADE_S:
		case MOD_GRENADE_SPLASH_W:
		case MOD_GRENADE_SPLASH_S:
			flag = COMBO_FLAG( WEAP_GRENADELAUNCHER );
			break;
		case MOD_PLASMA_W:
		case MOD_PLASMA_S:
		case MOD_PLASMA_SPLASH_W:
		case MOD_PLASMA_SPLASH_S:
			flag = COMBO_FLAG( WEAP_PLASMAGUN );
			break;
		case MOD_LASERGUN_W:
		case MOD_LASERGUN_S:
			flag = COMBO_FLAG( WEAP_LASERGUN );
			break;
		default:
			break;
	}

	if( flag ) {
		if( attacker->r.client->resp.awardInfo.combo[PLAYERNUM( targ )] == COMBO_FLAG( WEAP_ROCKETLAUNCHER ) && G_IsDead( targ ) ) { // RL...
			if( flag == COMBO_FLAG( WEAP_ELECTROBOLT ) ) { // to EB
				G_PlayerAward( attacker, S_COLOR_BLUE "RL to EB!" );
			} else if( flag == COMBO_FLAG( WEAP_LASERGUN ) ) { // to LG
				G_PlayerAward( attacker, S_COLOR_BLUE "RL to LG!" );
			} else if( flag == COMBO_FLAG( WEAP_RIOTGUN ) ) { // to RG
				G_PlayerAward( attacker, S_COLOR_BLUE "RL to RG!" );
			} else if( flag == COMBO_FLAG( WEAP_GRENADELAUNCHER ) ) { // to GL
				G_PlayerAward( attacker, S_COLOR_BLUE "RL to GL!" );
			}

			//else if( flag == COMBO_FLAG( WEAP_ROCKETLAUNCHER ) )  // to RL
			//	G_PlayerAward( attacker, S_COLOR_BLUE "RL to RL!" );
		} else if( attacker->r.client->resp.awardInfo.combo[PLAYERNUM( targ )] == COMBO_FLAG( WEAP_GRENADELAUNCHER ) && G_IsDead( targ ) ) {   // GL...
			if( flag == COMBO_FLAG( WEAP_ELECTROBOLT ) ) { // to EB
				G_PlayerAward( attacker, S_COLOR_BLUE "GL to EB!" );
			} else if( flag == COMBO_FLAG( WEAP_LASERGUN ) ) { // to LG
				G_PlayerAward( attacker, S_COLOR_BLUE "GL to LG!" );
			} else if( flag == COMBO_FLAG( WEAP_RIOTGUN ) ) { // to RG
				G_PlayerAward( attacker, S_COLOR_BLUE "GL to RG!" );
			} else if( flag == COMBO_FLAG( WEAP_ROCKETLAUNCHER ) ) { // to RL
				G_PlayerAward( attacker, S_COLOR_BLUE "GL to RL!" );
			}

			//else if( flag == COMBO_FLAG( WEAP_GRENADELAUNCHER ) )  // to GL
			//	G_PlayerAward( attacker, S_COLOR_BLUE "GL to GL!" );
		} else if( attacker->r.client->resp.awardInfo.combo[PLAYERNUM( targ )] == COMBO_FLAG( WEAP_LASERGUN ) && G_IsDead( targ ) ) {   // LG...
			if( flag == COMBO_FLAG( WEAP_ELECTROBOLT ) ) { // to EB
				if( attacker->r.client->resp.awardInfo.lasthit == targ && level.time < attacker->r.client->resp.awardInfo.lasthit_time + LB_TIMEOUT_FOR_COMBO ) {
					G_PlayerAward( attacker, S_COLOR_BLUE "LG to EB!" );
				}
			}
		} else if( attacker->r.client->resp.awardInfo.combo[PLAYERNUM( targ )] == COMBO_FLAG( WEAP_GUNBLADE ) && G_IsDead( targ ) ) {
			if( flag == COMBO_FLAG( WEAP_GUNBLADE ) ) {
				if( attacker->r.client->resp.awardInfo.lasthit == targ && level.time < attacker->r.client->resp.awardInfo.lasthit_time + GUNBLADE_TIMEOUT_FOR_COMBO ) {
					G_PlayerAward( attacker, S_COLOR_BLUE "Gunblade Combo!" );
				}
			}
		}

		attacker->r.client->resp.awardInfo.combo[PLAYERNUM( targ )] = flag;
	}

	attacker->r.client->resp.awardInfo.lasthit = targ;
	attacker->r.client->resp.awardInfo.lasthit_time = level.time;
}

void G_AwardResetPlayerComboStats( edict_t *ent ) {
	int i;
	int resetvalue;

	// combo from LB can be cancelled only if player's dead, if he missed or if he hasnt shot with LB for too long
	resetvalue = ( G_IsDead( ent ) ? 0 : COMBO_FLAG( WEAP_LASERGUN ) );

	for( i = 0; i < gs.maxclients; i++ )
		game.clients[i].resp.awardInfo.combo[PLAYERNUM( ent )] &= resetvalue;
}

void G_AwardPlayerMissedElectrobolt( edict_t *self, int mod ) {
	if( mod == MOD_ELECTROBOLT_W || mod == MOD_ELECTROBOLT_S || mod == MOD_INSTAGUN_W || mod == MOD_INSTAGUN_S ) {
		self->r.client->resp.awardInfo.ebhit_count = 0;
	}
}

void G_AwardPlayerMissedLasergun( edict_t *self, int mod ) {
	int i;
	if( mod == MOD_LASERGUN_W || mod == MOD_LASERGUN_S ) {
		for( i = 0; i < gs.maxclients; i++ )  // cancelling lasergun combo award
			game.clients[i].resp.awardInfo.combo[PLAYERNUM( self )] &= ~COMBO_FLAG( WEAP_LASERGUN );
	}
}

void G_AwardPlayerKilled( edict_t *self, edict_t *inflictor, edict_t *attacker, int mod ) {
	trace_t trace;

	if( self->r.svflags & SVF_CORPSE ) {
		return;
	}

	// First let the StatsowFacade decide whether to log the frag
	StatsowFacade::Instance()->AddFrag( attacker, self, mod );

	if( !attacker->r.client ) {
		return;
	}

	if( !self->r.client ) {
		return;
	}

	if( attacker == self ) {
		return;
	}

	if( attacker->s.team == self->s.team && attacker->s.team > TEAM_PLAYERS ) {
		return;
	}

	static const int strongMeansOfDeath[3] = { MOD_ROCKET_S, MOD_GRENADE_S, MOD_SHOCKWAVE_S };
	static const int weakMeansOfDeath[3] = { MOD_ROCKET_W, MOD_GRENADE_W, MOD_SHOCKWAVE_W };
	static const char *weaponNames[3] = { "Rocket", "Grenade", "Shockwave" };

	award_info_t *awardInfo = &attacker->r.client->resp.awardInfo;
	int *const directAwardCounts[3] = {
		&awardInfo->directrocket_award, &awardInfo->directgrenade_award, &awardInfo->directwave_award
	};
	int *const midairAwardCounts[3] = {
		&awardInfo->rl_midair_award, &awardInfo->gl_midair_award, &awardInfo->sw_midair_award
	};

	for( int i = 0; i < 3; ++i ) {
		if( mod == weakMeansOfDeath[i] || mod == strongMeansOfDeath[i] ) {
			( *directAwardCounts[i] )++;
			G_PlayerAward( attacker, va( S_COLOR_BLUE "Direct %s Hit!", weaponNames[i] ) );

			// Midair
			if( !self->groundentity && !self->waterlevel ) {
				// check for height to the ground
				float *groundPoint = tv( self->s.origin[1], self->s.origin[1], self->s.origin[2] - 64 );
				G_Trace( &trace, self->s.origin, self->r.mins, self->r.maxs, groundPoint, self, MASK_SOLID );
				if( trace.fraction == 1.0f ) {
					( *midairAwardCounts[i] )++;
					G_PlayerAward( attacker, va( S_COLOR_BLUE "Air %s!", weaponNames[i] ) );
				}
			}

			break;
		}
	}

	// Multikill
	if( game.serverTime - attacker->r.client->resp.awardInfo.multifrag_timer < MULTIKILL_INTERVAL ) {
		attacker->r.client->resp.awardInfo.multifrag_count++;
	} else {
		attacker->r.client->resp.awardInfo.multifrag_count = 1;
	}

	attacker->r.client->resp.awardInfo.multifrag_timer = game.serverTime;

	if( attacker->r.client->resp.awardInfo.multifrag_count > 1 ) {
		char s[MAX_CONFIGSTRING_CHARS];

		s[0] = 0;

		switch( attacker->r.client->resp.awardInfo.multifrag_count ) {
			case 0:
			case 1:
				break;
			case 2:
				Q_strncpyz( s, S_COLOR_GREEN "Double Frag!", sizeof( s ) );
				break;
			case 3:
				Q_strncpyz( s, S_COLOR_GREEN "Triple Frag!", sizeof( s ) );
				break;
			case 4:
				Q_strncpyz( s, S_COLOR_GREEN "Quadruple Frag!", sizeof( s ) );
				break;
			default:
				Q_snprintfz( s, sizeof( s ), S_COLOR_GREEN "Extermination! %i in a row!", attacker->r.client->resp.awardInfo.multifrag_count );
				break;
		}

		G_PlayerAward( attacker, s );
	}

	// Sprees
	attacker->r.client->resp.awardInfo.frag_count++;

	if( attacker->r.client->resp.awardInfo.frag_count &&
		( attacker->r.client->resp.awardInfo.frag_count % 5 == 0 ) ) {
		char s[MAX_CONFIGSTRING_CHARS];

		s[0] = 0;

		switch( (int)( attacker->r.client->resp.awardInfo.frag_count / 5 ) ) {
			case 1:
				Q_strncpyz( s, S_COLOR_YELLOW "On Fire!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is On Fire!\n", attacker->r.client->netname );
				break;
			case 2:
				Q_strncpyz( s, S_COLOR_YELLOW "Raging!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is Raging!\n", attacker->r.client->netname );
				break;
			case 3:
				Q_strncpyz( s, S_COLOR_YELLOW "Fraglord!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is the Fraglord!\n", attacker->r.client->netname );
				break;
			case 4:
				Q_strncpyz( s, S_COLOR_YELLOW "Extermination!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is Exterminating!\n", attacker->r.client->netname );
				break;
			case 5:
				Q_strncpyz( s, S_COLOR_YELLOW "God Mode!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is in God Mode!\n", attacker->r.client->netname );
			default:
				Q_strncpyz( s, S_COLOR_YELLOW "God Mode!", sizeof( s ) );
				G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " is in God Mode! " S_COLOR_WHITE "%d" S_COLOR_YELLOW " frags!\n", attacker->r.client->netname, attacker->r.client->resp.awardInfo.frag_count );
				break;
		}

		G_PlayerAward( attacker, s );
	}

	if( teamlist[attacker->s.team].stats.GetEntry( "frags" ) == 1 ) {
		int i;

		for( i = TEAM_PLAYERS; i < GS_MAX_TEAMS; i++ ) {
			if( i == attacker->s.team ) {
				continue;
			}
			if( teamlist[i].stats.GetEntry( "frags" ) ) {
				break;
			}
		}

		if( i != GS_MAX_TEAMS ) {
			G_PlayerAward( attacker, S_COLOR_YELLOW "First Frag!" );
		}
	}

	// ch : weapon specific frags
	if( G_ModToAmmo( mod ) != AMMO_NONE ) {
		attacker->r.client->level.stats.accuracy_frags[G_ModToAmmo( mod ) - AMMO_GUNBLADE]++;
	}
}

void G_AwardPlayerPickup( edict_t *self, edict_t *item ) {
	if( !item ) {
		return;
	}

	// MH control
	if( item->item->tag == HEALTH_MEGA ) {
		self->r.client->level.stats.AddToEntry( "mh_taken", 1 );
		self->r.client->resp.awardInfo.mh_control_award++;
		if( self->r.client->resp.awardInfo.mh_control_award % 5 == 0 ) {
			G_PlayerAward( self, S_COLOR_CYAN "Mega-Health Control!" );
		}
	}
	// UH control
	else if( item->item->tag == HEALTH_ULTRA ) {
		self->r.client->level.stats.AddToEntry( "uh_taken", 1 );
		self->r.client->resp.awardInfo.uh_control_award++;
		if( self->r.client->resp.awardInfo.uh_control_award % 5 == 0 ) {
			G_PlayerAward( self, S_COLOR_CYAN "Ultra-Health Control!" );
		}
	}
	// RA control
	else if( item->item->tag == ARMOR_RA ) {
		self->r.client->level.stats.AddToEntry( "ra_taken", 1 );
		self->r.client->resp.awardInfo.ra_control_award++;
		if( self->r.client->resp.awardInfo.ra_control_award % 5 == 0 ) {
			G_PlayerAward( self, S_COLOR_CYAN "Red Armor Control!" );
		}
	}
	// Other items counts
	else if( item->item->tag == ARMOR_GA ) {
		self->r.client->level.stats.AddToEntry( "ga_taken", 1 );
	} else if( item->item->tag == ARMOR_YA ) {
		self->r.client->level.stats.AddToEntry( "ya_taken", 1 );
	} else if( item->item->tag == POWERUP_QUAD ) {
		self->r.client->level.stats.AddToEntry( "quads_taken", 1 );
	} else if( item->item->tag == POWERUP_REGEN ) {
		self->r.client->level.stats.AddToEntry( "regens_taken", 1 );
	} else if( item->item->tag == POWERUP_SHELL ) {
		self->r.client->level.stats.AddToEntry( "shells_taken", 1 );
	}
}

void G_AwardRaceRecord( edict_t *self ) {
	G_PlayerAward( self, S_COLOR_CYAN "New Record!" );
}

void G_DeathAwards( edict_t *ent ) {
	int frag_count = ent->r.client->resp.awardInfo.frag_count;
	if( frag_count >= 5 ) {
		G_PrintMsg( NULL, "%s" S_COLOR_YELLOW " made a spree of " S_COLOR_WHITE "%d" S_COLOR_YELLOW "!\n", ent->r.client->netname, frag_count );
	}
}
