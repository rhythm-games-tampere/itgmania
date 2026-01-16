#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>

#include "global.h"
#include "SyncStartManager.h"
#include "SongManager.h"
#include "ScreenSelectMusic.h"
#include "PlayerNumber.h"
#include "ProfileManager.h"
#include "RageLog.h"
#include "GameState.h"
#include "CommonMetrics.h"

SyncStartManager *SYNCMAN;

#define BUFSIZE 1024
#define PORT 53000

// opcodes

#define START 0x00
#define SONG 0x01
#define SCORE 0x02
#define MARATHON_SONG_LOADING 0x03
#define MARATHON_SONG_READY 0x04
#define FINAL_SCORE 0x05
#define FINAL_COURSE_SCORE 0x06

#define MISC_ITEMS_LENGTH 10
#define ALL_ITEMS_LENGTH (MISC_ITEMS_LENGTH + NUM_TapNoteScore + 1 + NUM_HoldNoteScore) // add 1 for white count

std::vector<std::string> split(const std::string& str, const std::string& delim) {
	std::vector<std::string> tokens;
	tokens.reserve(ALL_ITEMS_LENGTH);
	size_t prev = 0, pos = 0;

	do
	{
		pos = str.find(delim, prev);
		if (pos == std::string::npos) pos = str.length();
		std::string token = str.substr(prev, pos-prev);
		tokens.push_back(token);
		prev = pos + delim.length();
	}
	while (pos < str.length() && prev < str.length());

	return tokens;
}

std::string SongToString(const Song& song) {
	RString sDir = song.GetSongDir();
	sDir.Replace("\\","/");
	std::vector<RString> bits;
	split(sDir, "/", bits);

	return song.m_sGroupName + '/' + *bits.rbegin();
}

std::string CourseToString(const Course& course) {
	if (course.m_sPath.empty()) {
		return "";
	}

	RString sDir = course.m_sPath;
	sDir.Replace("\\","/");
	std::vector<RString> bits;
	split(sDir, "/", bits);

	return course.m_sGroupName + '/' + *bits.rbegin();
}

SyncStartManager::SyncStartManager()
{
	// Register with Lua.
	{
		Lua *L = LUA->Get();
		lua_pushstring( L, "SYNCMAN" );
		this->PushSelf( L );
		lua_settable(L, LUA_GLOBALSINDEX);
		LUA->Release( L );
	}

	this->socket = {};
	this->enabled = false;
    this->machinesLoadingNextSongCounter = 0;
}

SyncStartManager::~SyncStartManager()
{
	this->disable();
}

bool SyncStartManager::isEnabled() const
{
	return this->enabled;
}

void SyncStartManager::enable()
{
	// initialize
	RString listenAddress = ssprintf("0.0.0.0:%d", PORT);
	this->socket.reset(BroadcastSocket::Listen(listenAddress));
	if( this->socket ) this->enabled = true;
}

void SyncStartManager::broadcast(char code, const std::string& msg) {
	if (!this->enabled) return;

	// first byte is code, rest is the message
	char buffer[BUFSIZE];
	buffer[0] = code;
	std::size_t length = msg.copy(buffer + 1, BUFSIZE - 1, 0);

	#ifdef DEBUG
		LOG->Info("BROADCASTING: code %d, msg: '%s'", code, msg.c_str());
	#endif

	this->socket->Broadcast(buffer, length + 1);
}

void SyncStartManager::broadcastStarting()
{
	if (!this->activeSyncStartSong.empty()) {
		this->broadcast(START, this->activeSyncStartSong);
	}
}

void SyncStartManager::broadcastSelectedSong(const Song& song) {
	this->broadcast(SONG, SongToString(song));
}

void SyncStartManager::broadcastSelectedCourse(const Course& course) {
	this->broadcast(SONG, CourseToString(course));
}

void SyncStartManager::broadcastScoreChange(const PlayerStageStats& pPlayerStageStats,
	int w0Count, int w1Count, int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
	int currentDp, int possibleDp, const std::string& scoreStr)
{
	std::stringstream msg = writeScoreMessage(pPlayerStageStats, false, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
    this->broadcast(SCORE, msg.str());
}

void SyncStartManager::broadcastFinalScore(const PlayerStageStats& pPlayerStageStats,
	int w0Count, int w1Count, int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
	int currentDp, int possibleDp, const std::string& scoreStr)
{
	std::stringstream msg = writeScoreMessage(pPlayerStageStats, false, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
    this->broadcast(FINAL_SCORE, msg.str());
}

void SyncStartManager::broadcastFinalCourseScore(const PlayerStageStats& pPlayerStageStats,
	int w0Count, int w1Count, int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
	int currentDp, int possibleDp, const std::string& scoreStr)
{
	std::stringstream msg = writeScoreMessage(pPlayerStageStats, true, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
    this->broadcast(FINAL_COURSE_SCORE, msg.str());
}

std::stringstream SyncStartManager::writeScoreMessage(const PlayerStageStats& pPlayerStageStats, bool isCourseScore,
	int w0Count, int w1Count, int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
	int currentDp, int possibleDp, const std::string& scoreStr) const
{
    std::stringstream msg;

    std::string playerName = PROFILEMAN->GetPlayerName(pPlayerStageStats.m_player_number);

    if (playerName.empty()) {
        playerName = "NoName";
    }

    if (isCourseScore && GAMESTATE->IsCourseMode()) {
        Course* currentCourse = GAMESTATE->m_pCurCourse;

        if (currentCourse != nullptr) {
            std::string courseString = CourseToString(*currentCourse);
            msg << courseString << '|';
        }
        else {
            msg << "NullCourse" << '|';
        }
    }
    else {
        msg << activeSyncStartSong << '|';
    }

    msg << (int) pPlayerStageStats.m_player_number << '|';
    msg << playerName << '|';
    msg << currentDp << '|';
    msg << possibleDp << '|';
    msg << possibleDp << '|';
    msg << scoreStr << '|';
    msg << pPlayerStageStats.GetCurrentLife() << '|';
    msg << (pPlayerStageStats.m_bFailed ? '1' : '0') << '|';

	// Tap note score values, get judgment counts from theme side to make rescored judgments work
	msg << pPlayerStageStats.m_iTapNoteScores[TNS_None] << '|';
	msg << pPlayerStageStats.m_iTapNoteScores[TNS_HitMine] << '|';
	msg << pPlayerStageStats.m_iTapNoteScores[TNS_AvoidMine] << '|';
	msg << pPlayerStageStats.m_iTapNoteScores[TNS_CheckpointMiss] << '|';
	msg << missCount << '|';
	msg << w5Count << '|';
	msg << w4Count << '|';
	msg << w3Count << '|';
	msg << w2Count << '|';
	msg << w1Count << '|';
	msg << w0Count << '|';
	msg << pPlayerStageStats.m_iTapNoteScores[TNS_CheckpointHit] << '|';

    for (int m_iHoldNoteScore : pPlayerStageStats.m_iHoldNoteScores) {
        msg << m_iHoldNoteScore << '|';
    }

	int possibleHolds = (int) pPlayerStageStats.m_radarPossible[RadarCategory_Holds];
	int possibleRolls = (int) pPlayerStageStats.m_radarPossible[RadarCategory_Rolls];
	int totalHolds = possibleHolds + possibleRolls;

	msg << totalHolds;

    return msg;
}

void SyncStartManager::broadcastMarathonSongLoading() {
    this->broadcast(MARATHON_SONG_LOADING, "");
}

void SyncStartManager::broadcastMarathonSongReady() {
    this->broadcast(MARATHON_SONG_READY, "");
}

void SyncStartManager::receiveScoreChange(const std::string& addr, const std::string& msg) {
	if (this->activeSyncStartSong.empty()) {
		return;
	}

	ScorePlayer scorePlayer = {};
	scorePlayer.machineAddress = addr;

	ScoreData scoreData;

	try {
		std::vector<std::string> items = split(msg, "|");

		// ignore messages that don't fit the size the message should be
		if (items.size() != ALL_ITEMS_LENGTH) {
			return;
		}

		auto iter = items.begin();

		// ignore scores for other than current song
		std::string& songName = *iter++;
		if (songName != this->activeSyncStartSong) {
			return;
		}

		scorePlayer.playerNumber = (PlayerNumber) std::stoi(*iter++);
		scorePlayer.playerName = *iter++;
		scoreData.actualDancePoints = std::stoi(*iter++);
		scoreData.currentPossibleDancePoints = std::stoi(*iter++);
		scoreData.possibleDancePoints = std::stoi(*iter++);
		scoreData.formattedScore = *iter++;
		scoreData.life = std::stof(*iter++);
		scoreData.failed = *iter++ == "1";

		for (int & tapNoteScore : scoreData.tapNoteScores) {
			tapNoteScore = std::stoi(*iter++);
		}

		for (int & holdNoteScore : scoreData.holdNoteScores) {
			holdNoteScore = std::stoi(*iter++);
		}

		scoreData.totalHolds = std::stoi(*iter++);

		this->syncStartScoreKeeper.AddScore(scorePlayer, scoreData);
		MESSAGEMAN->Broadcast("SyncStartPlayerScoresChanged");
	} catch (std::exception& e) {
		// just don't crash!
		LOG->Warn("Could not parse score change '%s'", msg.c_str());
	}
}

void SyncStartManager::disable()
{
	this->socket.reset();
	this->enabled = false;
}

int SyncStartManager::getNextMessage(char* buffer, std::string& remaddr, size_t bufferSize) {
	return this->socket->Receive(buffer, bufferSize, remaddr, 0);
}

void SyncStartManager::Update() {
	if (!this->enabled) {
		return;
	}

	char buffer[BUFSIZE];
	int received;
	std::string remaddr;

	// loop through packets received
	do {
		received = getNextMessage(buffer, remaddr, sizeof(buffer));
		if (received > 0) {
			char opcode = buffer[0];
			std::string msg = std::string(buffer + 1, received - 1);

			if (opcode == SONG && this->waitingForSongChanges) {
				this->songOrCourseWaitingToBeChangedTo = msg;
			} else if (opcode == START && this->waitingForSynchronizedStarting) {
				if (msg == activeSyncStartSong) {
					this->shouldStart = true;
				}
			} else if (opcode == SCORE) {
				this->receiveScoreChange(remaddr, msg);
            } else if (opcode == MARATHON_SONG_LOADING) {
                this->machinesLoadingNextSongCounter++;
                LOG->Info("MARATHON_SONG_LOADING, counter=%d", this->machinesLoadingNextSongCounter);
            } else if (opcode == MARATHON_SONG_READY) {
                this->machinesLoadingNextSongCounter--;
                LOG->Info("MARATHON_SONG_READY, counter=%d", this->machinesLoadingNextSongCounter);
                if (this->machinesLoadingNextSongCounter == 0) {
                    this->shouldStart = true;
                }
            }
		}
	} while (received > 0);
}

std::vector<SyncStartScore> SyncStartManager::GetCurrentPlayerScores() {
	return this->syncStartScoreKeeper.GetScores(false);
}

std::vector<SyncStartScore> SyncStartManager::GetLatestPlayerScores() {
	return this->syncStartScoreKeeper.GetScores(true);
}

void SyncStartManager::ListenForSongChanges(bool enabled) {
	LOG->Info("Listen for song changes: %d", enabled);
	this->waitingForSongChanges = enabled;
	this->songOrCourseWaitingToBeChangedTo = "";
}

std::string SyncStartManager::GetSongOrCourseToChangeTo() {
	std::string songOrCourse = this->songOrCourseWaitingToBeChangedTo;

	if (!songOrCourse.empty()) {
		this->songOrCourseWaitingToBeChangedTo = "";
		return songOrCourse;
	} else {
		return "";
	}
}

void SyncStartManager::StartListeningForSynchronizedStart(const Song& song) {
	this->syncStartScoreKeeper.ResetScores();
	this->activeSyncStartSong = SongToString(song);
	this->shouldStart = false;
	this->waitingForSynchronizedStarting = true;
}

void SyncStartManager::StopListeningForSynchronizedStart() {
	this->shouldStart = false;
	this->waitingForSynchronizedStarting = false;
}

bool SyncStartManager::AttemptStart() {
    if (this->shouldStart) {
        this->machinesLoadingNextSongCounter = 0;
        this->shouldStart = false;
        return true;
    } else {
        return false;
    }
}

void SyncStartManager::SongChangedDuringGameplay(const Song& song) {
	this->activeSyncStartSong = SongToString(song);
}

void SyncStartManager::StopListeningScoreChanges() {
	this->activeSyncStartSong = "";
}

// lua start
#include "LuaBinding.h"

class LunaSyncStartManager: public Luna<SyncStartManager> {
	public:
		static void PushScores( T* p, lua_State *L, const std::vector<SyncStartScore>& scores )
		{
			lua_newtable( L );
			int outer_table_index = lua_gettop(L);

			int i = 0;
			for (auto score = scores.begin(); score != scores.end(); score++) {
				lua_newtable( L );
				int inner_table_index = lua_gettop(L);

				lua_pushstring(L, "playerName");
				lua_pushstring(L, score->player.playerName.c_str());
				lua_settable(L, inner_table_index);

				lua_pushstring(L, "score");
				lua_pushstring(L, score->data.formattedScore.c_str());
				lua_settable(L, inner_table_index);

				lua_pushstring(L, "failed");
				lua_pushboolean(L, score->data.failed);
				lua_settable(L, inner_table_index);

				lua_rawseti(L, outer_table_index, i + 1);
				i++;
			}
		}

		static int IsEnabled( T* p, lua_State *L )
		{
			lua_pushboolean(L, p->isEnabled());
			return 1;
		}

		static int GetCurrentPlayerScores( T* p, lua_State *L )
		{
			auto scores = p->GetCurrentPlayerScores();
			PushScores(p, L, scores);
			return 1;
		}

		static int GetLatestPlayerScores( T* p, lua_State *L )
		{
			auto scores = p->GetLatestPlayerScores();
			PushScores(p, L, scores);
			return 1;
		}

		static int BroadcastScoreChange( T* p, lua_State *L )
		{
			const PlayerStageStats* playerStageStats = Luna<PlayerStageStats>::check(L,1);
			const int w0Count = IArg(2);
			const int w1Count = IArg(3);
			const int w2Count = IArg(4);
			const int w3Count = IArg(5);
			const int w4Count = IArg(6);
			const int w5Count = IArg(7);
			const int missCount = IArg(8);
			const int currentDp = IArg(9);
			const int possibleDp = IArg(10);
			const std::string scoreStr = SArg(11);
			p->broadcastScoreChange(*playerStageStats, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
			return 1;
		}

        static int BroadcastFinalScore( T* p, lua_State *L )
        {
            const PlayerStageStats* playerStageStats = Luna<PlayerStageStats>::check(L,1);
			const int w0Count = IArg(2);
			const int w1Count = IArg(3);
			const int w2Count = IArg(4);
			const int w3Count = IArg(5);
			const int w4Count = IArg(6);
			const int w5Count = IArg(7);
			const int missCount = IArg(8);
			const int currentDp = IArg(9);
			const int possibleDp = IArg(10);
			const std::string scoreStr = SArg(11);
            p->broadcastFinalScore(*playerStageStats, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
            return 1;
        }

        static int BroadcastFinalCourseScore( T* p, lua_State *L )
        {
            const PlayerStageStats* playerStageStats = Luna<PlayerStageStats>::check(L,1);
			const int w0Count = IArg(2);
			const int w1Count = IArg(3);
			const int w2Count = IArg(4);
			const int w3Count = IArg(5);
			const int w4Count = IArg(6);
			const int w5Count = IArg(7);
			const int missCount = IArg(8);
			const int currentDp = IArg(9);
			const int possibleDp = IArg(10);
			const std::string scoreStr = SArg(11);
            p->broadcastFinalCourseScore(*playerStageStats, w0Count, w1Count, w2Count, w3Count, w4Count, w5Count, missCount, currentDp, possibleDp, scoreStr);
            return 1;
        }

		LunaSyncStartManager()
		{
			ADD_METHOD(IsEnabled);
			ADD_METHOD(GetCurrentPlayerScores);
			ADD_METHOD(GetLatestPlayerScores);
			ADD_METHOD(BroadcastScoreChange);
            ADD_METHOD(BroadcastFinalScore);
            ADD_METHOD(BroadcastFinalCourseScore);
		}
};

LUA_REGISTER_CLASS( SyncStartManager )
