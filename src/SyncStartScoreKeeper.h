#ifndef SRC_SYNCSTARTSCOREKEEPER_H
#define SRC_SYNCSTARTSCOREKEEPER_H

#include <string>
#include <arpa/inet.h>

#include "PlayerNumber.h"
#include "PlayerStageStats.h"


struct ScorePlayer {
	struct in_addr machineAddress;
	PlayerNumber playerNumber;
	std::string playerName;
};

struct ScoreData {
	int actualDancePoints;
    int currentPossibleDancePoints;
    int possibleDancePoints;
    std::string formattedScore;
	float life;
	bool failed;
	int tapNoteScores[NUM_TapNoteScore + 1]; // White count added
	int holdNoteScores[NUM_HoldNoteScore];
	int totalHolds;
};

struct SyncStartScore {
    ScorePlayer player;
    ScoreData data;
};

class SyncStartScoreKeeper {
    private:
        std::map<ScorePlayer, ScoreData> scoreBuffer;

    public:
        void AddScore(const ScorePlayer& scorePlayer, const ScoreData& scoreData);
        std::vector<SyncStartScore> GetScores(bool latestValues);
        void ResetScores();
};

#endif /* SRC_SYNCSTARTSCOREKEEPER_H */
