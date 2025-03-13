#include <algorithm>

#include "global.h"
#include "SyncStartScoreKeeper.h"
#include "NoteTypes.h"

#define MAX_POSSIBLE_DANCE_POINTS_DIFFERENCE 100

bool operator<(const ScorePlayer& l, const ScorePlayer& r) {
	return std::tie(l.machineAddress.s_addr, l.playerNumber) < std::tie(r.machineAddress.s_addr, r.playerNumber);
}

float getScore(const SyncStartScore& score) {
	if (score.data.possibleDancePoints == 0) {
		return 0;
	}

	return clamp(score.data.actualDancePoints / (float) score.data.possibleDancePoints, 0.0f, 1.0f);
}

int getLostDancePoints(const SyncStartScore& score) {
	return score.data.currentPossibleDancePoints - score.data.actualDancePoints;
}

bool sortByScore(const SyncStartScore& l, const SyncStartScore& r) {
	auto rScore = getScore(r);
	auto lScore = getScore(l);
	auto rFailed = r.data.failed ? 0 : 1;
	auto lFailed = l.data.failed ? 0 : 1;
	return std::tie(rFailed, rScore) < std::tie(lFailed, lScore);
}

bool sortByLostScore(const SyncStartScore& l, const SyncStartScore& r) {
	// if a connection to a machine is dropped or something, don't keep its position by lost score
	if (abs(l.data.currentPossibleDancePoints - r.data.currentPossibleDancePoints) > MAX_POSSIBLE_DANCE_POINTS_DIFFERENCE) {
		return sortByScore(l, r);
	}

	// negation so order is correct. unlike in score, less lost dance points is better.
	auto rScore = -getLostDancePoints(r);
	auto lScore = -getLostDancePoints(l);
	auto rFailed = r.data.failed ? 0 : 1;
	auto lFailed = l.data.failed ? 0 : 1;

	return std::tie(rFailed, rScore) < std::tie(lFailed, lScore);
}

void SyncStartScoreKeeper::AddScore(const ScorePlayer& scorePlayer, const ScoreData& scoreData) {
	scoreBuffer[scorePlayer] = scoreData;
}

std::vector<SyncStartScore> SyncStartScoreKeeper::GetScores(bool latestValues) {
	std::vector<SyncStartScore> scores;
	scores.reserve(scoreBuffer.size());

	for (auto iter = scoreBuffer.begin(); iter != scoreBuffer.end(); iter++) {
		scores.emplace_back(SyncStartScore {
			.player = iter->first,
			.data = iter->second
		});
	}

	std::sort(scores.begin(), scores.end(), latestValues ? sortByScore : sortByLostScore);
	return scores;
}

void SyncStartScoreKeeper::ResetScores() {
	this->scoreBuffer.clear();
}

