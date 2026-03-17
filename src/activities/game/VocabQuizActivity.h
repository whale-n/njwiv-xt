#pragma once
#include <array>
#include <vector>

#include "../Activity.h"

class VocabQuizActivity final : public Activity {
 public:
  explicit VocabQuizActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("VocabQuiz", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class QuizState { QUESTION, REVEAL, ETYMOLOGY, SUMMARY };

  // Shuffled word indices (avoids repeats)
  std::vector<int> wordOrder;
  int currentWordIdx = 0;  // position in wordOrder

  // Current question
  int correctChoice = 0;       // which of the 4 choices is correct (0-3)
  int choiceIndices[4] = {};   // word pool indices for the 4 choices
  int selectedChoice = 0;      // currently highlighted choice (0-3)
  bool answered = false;       // has the user locked in an answer?
  bool wasCorrect = false;     // was the locked-in answer correct?

  // Stats
  int totalAnswered = 0;
  int totalCorrect = 0;
  int currentStreak = 0;
  int bestStreak = 0;

  // State
  QuizState state = QuizState::QUESTION;

  // Methods
  void shuffleWordOrder();
  void setupQuestion();
  void lockAnswer();
  void nextQuestion();

  // Rendering
  void drawQuestionScreen(GfxRenderer& r) const;
  void drawRevealScreen(GfxRenderer& r) const;
  void drawEtymologyScreen(GfxRenderer& r) const;
  void drawSummaryScreen(GfxRenderer& r) const;
  void drawHeader(GfxRenderer& r) const;
  void drawChoices(GfxRenderer& r, bool showCorrect) const;
  void drawButtonHints(GfxRenderer& r) const;
};
