#ifndef SRC_SYNCSTARTMANAGER_H_
#define SRC_SYNCSTARTMANAGER_H_

#include <memory>
#include <string>

#include "Course.h"
#include "PlayerNumber.h"
#include "PlayerStageStats.h"
#include "RageTimer.h"
#include "Song.h"
#include "SyncStartScoreKeeper.h"
#include "arch/Socket/Socket.h"

class SyncStartManager {
 private:
  std::unique_ptr<BroadcastSocket> m_socket;
  bool m_enabled;
  void broadcast(char code, const std::string& msg);
  int getNextMessage(char* buffer, std::string& remaddr, size_t bufferSize);

  bool m_waitingForSongChanges;
  std::string m_songOrCourseWaitingToBeChangedTo;
  bool m_waitingForSynchronizedStarting;
  std::string m_activeSyncStartSong;
  bool m_shouldStart;
  int64_t m_startTime;
  int m_machinesLoadingNextSongCounter;
  RageTimer m_broadcastMarathonSongReadyRequested;

  SyncStartScoreKeeper m_syncStartScoreKeeper;

 public:
  SyncStartManager();
  ~SyncStartManager();
  bool isEnabled() const;
  void enable();
  void disable();
  void broadcastStarting();
  void broadcastSelectedSong(const Song& song);
  void broadcastSelectedCourse(const Course& course);
  void broadcastScoreChange(
      const PlayerStageStats& pPlayerStageStats, int w0Count, int w1Count,
      int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
      int currentDp, int possibleDp, const std::string& scoreStr);
  void broadcastFinalScore(
      const PlayerStageStats& pPlayerStageStats, int w0Count, int w1Count,
      int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
      int currentDp, int possibleDp, const std::string& scoreStr);
  void broadcastFinalCourseScore(
      const PlayerStageStats& pPlayerStageStats, int w0Count, int w1Count,
      int w2Count, int w3Count, int w4Count, int w5Count, int missCount,
      int currentDp, int possibleDp, const std::string& scoreStr);
  [[nodiscard]] std::stringstream writeScoreMessage(
      const PlayerStageStats& pPlayerStageStats, bool isCourseScore,
      int w0Count, int w1Count, int w2Count, int w3Count, int w4Count,
      int w5Count, int missCount, int currentDp, int possibleDp,
      const std::string& scoreStr) const;
  void broadcastMarathonSongLoading();
  void broadcastMarathonSongReady();
  void receiveScoreChange(const std::string& addr, const std::string& msg);
  std::vector<SyncStartScore> GetCurrentPlayerScores();
  std::vector<SyncStartScore> GetLatestPlayerScores();
  void Update();
  void ListenForSongChanges(bool enabled);
  std::string GetSongOrCourseToChangeTo();
  void StartListeningForSynchronizedStart(const Song& song);
  void StopListeningForSynchronizedStart();
  bool AttemptStart(int64_t& startTime);
  void StopListeningScoreChanges();
  void SongChangedDuringGameplay(const Song& song);
  bool IsWaiting() const { return m_waitingForSynchronizedStarting; }

  // Lua
  void PushSelf(lua_State* L);
};

extern SyncStartManager* SYNCMAN;

#endif /* SRC_SYNCSTARTMANAGER_H_ */
