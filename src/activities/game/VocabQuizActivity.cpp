#include "VocabQuizActivity.h"

#include <algorithm>
#include <cstring>

#include "GfxRenderer.h"
#include "WordPool.h"
#include "components/UITheme.h"
#include "fontIds.h"

#if defined(ESP32)
#include <esp_random.h>
#endif

// Layout constants (portrait 480x800)
static constexpr int kHeaderH = 36;
static constexpr int kSidePad = 24;
static constexpr int kWordY = 60;
static constexpr int kPronounceY = 110;
static constexpr int kSentenceY = 140;
static constexpr int kChoicesStartY = 240;
static constexpr int kChoiceH = 70;
static constexpr int kChoiceGap = 8;
static constexpr int kChoiceRadius = 6;
static constexpr int kStatsY = 580;

static int randomInt(int max) {
#if defined(ESP32)
  return esp_random() % max;
#else
  return rand() % max;
#endif
}

void VocabQuizActivity::onEnter() {
  totalAnswered = 0;
  totalCorrect = 0;
  currentStreak = 0;
  bestStreak = 0;
  state = QuizState::QUESTION;
  shuffleWordOrder();
  setupQuestion();
  requestUpdate();
}

void VocabQuizActivity::onExit() {}

void VocabQuizActivity::shuffleWordOrder() {
  wordOrder.resize(vocab::kWordCount);
  for (int i = 0; i < vocab::kWordCount; i++) {
    wordOrder[i] = i;
  }
  // Fisher-Yates shuffle
  for (int i = vocab::kWordCount - 1; i > 0; i--) {
    int j = randomInt(i + 1);
    std::swap(wordOrder[i], wordOrder[j]);
  }
  currentWordIdx = 0;
}

void VocabQuizActivity::setupQuestion() {
  if (currentWordIdx >= static_cast<int>(wordOrder.size())) {
    shuffleWordOrder();
  }

  const int correctIdx = wordOrder[currentWordIdx];

  // Pick 3 unique wrong answers
  int wrongIndices[3];
  int wrongCount = 0;
  while (wrongCount < 3) {
    int r = randomInt(vocab::kWordCount);
    if (r == correctIdx) continue;
    bool dup = false;
    for (int i = 0; i < wrongCount; i++) {
      if (wrongIndices[i] == r) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      wrongIndices[wrongCount++] = r;
    }
  }

  // Place correct answer at random position
  correctChoice = randomInt(4);
  for (int i = 0, w = 0; i < 4; i++) {
    if (i == correctChoice) {
      choiceIndices[i] = correctIdx;
    } else {
      choiceIndices[i] = wrongIndices[w++];
    }
  }

  selectedChoice = 0;
  answered = false;
  wasCorrect = false;
}

void VocabQuizActivity::lockAnswer() {
  answered = true;
  wasCorrect = (selectedChoice == correctChoice);
  totalAnswered++;
  if (wasCorrect) {
    totalCorrect++;
    currentStreak++;
    if (currentStreak > bestStreak) bestStreak = currentStreak;
  } else {
    currentStreak = 0;
  }
  state = QuizState::REVEAL;
}

void VocabQuizActivity::nextQuestion() {
  currentWordIdx++;
  state = QuizState::QUESTION;
  setupQuestion();
}

// ── Input Handling ───────────────────────────────────────────────

void VocabQuizActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state == QuizState::SUMMARY) {
      state = QuizState::QUESTION;
      requestUpdate();
    } else if (state == QuizState::ETYMOLOGY) {
      // Skip etymology, go to next question
      nextQuestion();
      requestUpdate();
    } else if (state == QuizState::REVEAL) {
      // Skip etymology, go to next question
      nextQuestion();
      requestUpdate();
    } else if (totalAnswered > 0 && state == QuizState::QUESTION) {
      state = QuizState::SUMMARY;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (state == QuizState::QUESTION) {
    // Up/Down (side buttons) to navigate choices
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (selectedChoice > 0) {
        selectedChoice--;
        requestUpdate();
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (selectedChoice < 3) {
        selectedChoice++;
        requestUpdate();
      }
    }
    // Left/Right (front buttons) also navigate choices
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (selectedChoice > 0) {
        selectedChoice--;
        requestUpdate();
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (selectedChoice < 3) {
        selectedChoice++;
        requestUpdate();
      }
    }
    // Confirm = lock in answer
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      lockAnswer();
      requestUpdate();
    }
  } else if (state == QuizState::REVEAL) {
    // Confirm = go to etymology deep-dive
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = QuizState::ETYMOLOGY;
      requestUpdate();
    }
    // Left/Right = skip etymology, next question
    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      nextQuestion();
      requestUpdate();
    }
  } else if (state == QuizState::ETYMOLOGY) {
    // Any action = next question
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      nextQuestion();
      requestUpdate();
    }
  } else if (state == QuizState::SUMMARY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = QuizState::QUESTION;
      requestUpdate();
    }
  }
}

// ── Rendering ────────────────────────────────────────────────────

void VocabQuizActivity::drawHeader(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();

  // Black header bar
  r.fillRect(0, 0, pageW, kHeaderH, true);
  r.drawText(UI_12_FONT_ID, kSidePad, 10, "VOCAB QUIZ", false, EpdFontFamily::BOLD);

  // Score on right
  char scoreBuf[16];
  if (totalAnswered > 0) {
    const int pct = (totalCorrect * 100) / totalAnswered;
    snprintf(scoreBuf, sizeof(scoreBuf), "%d%%", pct);
  } else {
    snprintf(scoreBuf, sizeof(scoreBuf), "0%%");
  }
  const int sw = r.getTextWidth(UI_12_FONT_ID, scoreBuf, EpdFontFamily::BOLD);
  r.drawText(UI_12_FONT_ID, pageW - kSidePad - sw, 10, scoreBuf, false, EpdFontFamily::BOLD);
}

void VocabQuizActivity::drawChoices(GfxRenderer& r, bool showCorrect) const {
  const int pageW = r.getScreenWidth();
  const int choiceW = pageW - 2 * kSidePad;
  char label[4] = "A.";

  for (int i = 0; i < 4; i++) {
    const int y = kChoicesStartY + i * (kChoiceH + kChoiceGap);
    const bool isSelected = (i == selectedChoice);
    const bool isCorrect = (i == correctChoice);

    if (showCorrect) {
      if (isCorrect) {
        r.fillRoundedRect(kSidePad, y, choiceW, kChoiceH, kChoiceRadius, Color::Black);
      } else if (isSelected && !wasCorrect) {
        r.fillRoundedRect(kSidePad, y, choiceW, kChoiceH, kChoiceRadius, Color::LightGray);
        r.drawRoundedRect(kSidePad, y, choiceW, kChoiceH, 1, kChoiceRadius, true);
      } else {
        r.drawRoundedRect(kSidePad, y, choiceW, kChoiceH, 1, kChoiceRadius, true);
      }
    } else {
      if (isSelected) {
        r.fillRoundedRect(kSidePad, y, choiceW, kChoiceH, kChoiceRadius, Color::LightGray);
        r.drawRoundedRect(kSidePad, y, choiceW, kChoiceH, 2, kChoiceRadius, true);
      } else {
        r.drawRoundedRect(kSidePad, y, choiceW, kChoiceH, 1, kChoiceRadius, true);
      }
    }

    // Label (A. B. C. D.)
    label[0] = 'A' + i;
    const bool invertText = showCorrect && isCorrect;
    r.drawText(SMALL_FONT_ID, kSidePad + 10, y + 8, label, !invertText, EpdFontFamily::BOLD);

    // Definition text (wrapped to 3 lines max)
    const int defIdx = choiceIndices[i];
    const char* def = vocab::kWords[defIdx].definition;
    const int maxTextW = choiceW - 50;
    auto lines = r.wrappedText(SMALL_FONT_ID, def, maxTextW, 3);
    const int lineH = r.getLineHeight(SMALL_FONT_ID);
    int textY = y + 8;
    for (const auto& line : lines) {
      r.drawText(SMALL_FONT_ID, kSidePad + 32, textY, line.c_str(), !invertText);
      textY += lineH;
    }
  }
}

void VocabQuizActivity::drawQuestionScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  const int wordIdx = wordOrder[currentWordIdx];
  const auto& word = vocab::kWords[wordIdx];

  // Word (large, bold, centered)
  r.drawCenteredText(UI_12_FONT_ID, kWordY, word.word, true, EpdFontFamily::BOLD);

  // Pronunciation
  char pronBuf[64];
  snprintf(pronBuf, sizeof(pronBuf), "/%s/", word.pronunciation);
  r.drawCenteredText(SMALL_FONT_ID, kPronounceY, pronBuf);

  // Example sentence (wrapped, centered lines)
  if (word.exampleSentence && word.exampleSentence[0] != '\0') {
    const int maxTextW = pageW - 2 * kSidePad;
    auto lines = r.wrappedText(UI_10_FONT_ID, word.exampleSentence, maxTextW, 3);
    const int lineH = r.getLineHeight(UI_10_FONT_ID);
    int sentY = kSentenceY;
    for (const auto& line : lines) {
      const int tw = r.getTextWidth(UI_10_FONT_ID, line.c_str());
      r.drawText(UI_10_FONT_ID, (pageW - tw) / 2, sentY, line.c_str());
      sentY += lineH;
    }
  }

  // Divider
  r.drawLine(kSidePad, kChoicesStartY - 14, pageW - kSidePad, kChoicesStartY - 14);

  // Choices
  drawChoices(r, false);

  // Progress + streak
  char progBuf[32];
  snprintf(progBuf, sizeof(progBuf), "%d / %d", currentWordIdx + 1, vocab::kWordCount);
  r.drawCenteredText(SMALL_FONT_ID, kStatsY, progBuf);

  if (currentStreak > 1) {
    char streakBuf[32];
    snprintf(streakBuf, sizeof(streakBuf), "Streak: %d", currentStreak);
    r.drawCenteredText(SMALL_FONT_ID, kStatsY + 18, streakBuf);
  }
}

void VocabQuizActivity::drawRevealScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  const int wordIdx = wordOrder[currentWordIdx];
  const auto& word = vocab::kWords[wordIdx];

  // Word
  r.drawCenteredText(UI_12_FONT_ID, kWordY, word.word, true, EpdFontFamily::BOLD);

  // Pronunciation
  char pronBuf[64];
  snprintf(pronBuf, sizeof(pronBuf), "/%s/", word.pronunciation);
  r.drawCenteredText(SMALL_FONT_ID, kPronounceY, pronBuf);

  // Result feedback
  const char* feedback = wasCorrect ? "CORRECT!" : "INCORRECT";
  r.drawCenteredText(UI_12_FONT_ID, kSentenceY, feedback, true, EpdFontFamily::BOLD);

  // Show correct definition below feedback when wrong
  if (!wasCorrect) {
    const int maxTextW = pageW - 2 * kSidePad;
    auto lines = r.wrappedText(UI_10_FONT_ID, word.definition, maxTextW, 2);
    const int lineH = r.getLineHeight(UI_10_FONT_ID);
    int defY = kSentenceY + 24;
    for (const auto& line : lines) {
      const int tw = r.getTextWidth(UI_10_FONT_ID, line.c_str());
      r.drawText(UI_10_FONT_ID, (pageW - tw) / 2, defY, line.c_str());
      defY += lineH;
    }
  }

  // Divider
  r.drawLine(kSidePad, kChoicesStartY - 14, pageW - kSidePad, kChoicesStartY - 14);

  // Choices with correct answer highlighted
  drawChoices(r, true);

  // Stats
  char statsBuf[48];
  snprintf(statsBuf, sizeof(statsBuf), "%d correct of %d  (%d%%)", totalCorrect, totalAnswered,
           totalAnswered > 0 ? (totalCorrect * 100) / totalAnswered : 0);
  r.drawCenteredText(SMALL_FONT_ID, kStatsY, statsBuf);

  if (currentStreak > 1) {
    char streakBuf[32];
    snprintf(streakBuf, sizeof(streakBuf), "Streak: %d", currentStreak);
    r.drawCenteredText(SMALL_FONT_ID, kStatsY + 18, streakBuf);
  }
}

void VocabQuizActivity::drawEtymologyScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  const int wordIdx = wordOrder[currentWordIdx];
  const auto& word = vocab::kWords[wordIdx];
  const int maxTextW = pageW - 2 * kSidePad;

  // Word title
  r.drawCenteredText(UI_12_FONT_ID, kWordY, word.word, true, EpdFontFamily::BOLD);

  // Pronunciation
  char pronBuf[64];
  snprintf(pronBuf, sizeof(pronBuf), "/%s/", word.pronunciation);
  r.drawCenteredText(SMALL_FONT_ID, kPronounceY, pronBuf);

  // Definition (full, wrapped)
  int y = 140;
  r.drawText(SMALL_FONT_ID, kSidePad, y, "DEFINITION", true, EpdFontFamily::BOLD);
  y += 18;
  auto defLines = r.wrappedText(UI_10_FONT_ID, word.definition, maxTextW, 3);
  const int lineH = r.getLineHeight(UI_10_FONT_ID);
  for (const auto& line : defLines) {
    r.drawText(UI_10_FONT_ID, kSidePad, y, line.c_str());
    y += lineH;
  }

  // Example sentence
  y += 10;
  r.drawText(SMALL_FONT_ID, kSidePad, y, "USAGE", true, EpdFontFamily::BOLD);
  y += 18;
  if (word.exampleSentence && word.exampleSentence[0] != '\0') {
    auto sentLines = r.wrappedText(UI_10_FONT_ID, word.exampleSentence, maxTextW, 3);
    for (const auto& line : sentLines) {
      r.drawText(UI_10_FONT_ID, kSidePad, y, line.c_str());
      y += lineH;
    }
  }

  // Etymology
  y += 10;
  r.drawLine(kSidePad, y, pageW - kSidePad, y);
  y += 12;
  r.drawText(SMALL_FONT_ID, kSidePad, y, "ETYMOLOGY", true, EpdFontFamily::BOLD);
  y += 18;
  if (word.etymology && word.etymology[0] != '\0') {
    auto etymLines = r.wrappedText(UI_10_FONT_ID, word.etymology, maxTextW, 6);
    for (const auto& line : etymLines) {
      r.drawText(UI_10_FONT_ID, kSidePad, y, line.c_str());
      y += lineH;
    }
  }

  // Mnemonic hint if available
  if (word.mnemonic && word.mnemonic[0] != '\0') {
    y += 10;
    r.drawText(SMALL_FONT_ID, kSidePad, y, "REMEMBER", true, EpdFontFamily::BOLD);
    y += 18;
    auto mnLines = r.wrappedText(UI_10_FONT_ID, word.mnemonic, maxTextW, 3);
    for (const auto& line : mnLines) {
      r.drawText(UI_10_FONT_ID, kSidePad, y, line.c_str());
      y += lineH;
    }
  }
}

void VocabQuizActivity::drawSummaryScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  char buf[64];

  int y = 120;
  r.drawCenteredText(UI_12_FONT_ID, y, "QUIZ SUMMARY", true, EpdFontFamily::BOLD);

  y += 60;
  snprintf(buf, sizeof(buf), "%d", totalAnswered);
  r.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
  r.drawCenteredText(SMALL_FONT_ID, y + 22, "Questions Answered");

  y += 60;
  snprintf(buf, sizeof(buf), "%d%%", totalAnswered > 0 ? (totalCorrect * 100) / totalAnswered : 0);
  r.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
  r.drawCenteredText(SMALL_FONT_ID, y + 22, "Accuracy");

  y += 60;
  snprintf(buf, sizeof(buf), "%d", bestStreak);
  r.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
  r.drawCenteredText(SMALL_FONT_ID, y + 22, "Best Streak");

  y += 60;
  snprintf(buf, sizeof(buf), "%d / %d words seen", currentWordIdx, vocab::kWordCount);
  r.drawCenteredText(UI_10_FONT_ID, y, buf);

  y += 50;
  r.drawLine(kSidePad, y, pageW - kSidePad, y);
  y += 16;
  r.drawCenteredText(SMALL_FONT_ID, y, "Press OK to continue  |  Back to exit");
}

void VocabQuizActivity::drawButtonHints(GfxRenderer& r) const {
  if (state == QuizState::QUESTION) {
    const auto labels = mappedInput.mapLabels("Stats", "Select", "<", ">");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(r, "Up", "Down");
  } else if (state == QuizState::REVEAL) {
    const auto labels = mappedInput.mapLabels("Skip", "Learn More", "", "Skip");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == QuizState::ETYMOLOGY) {
    const auto labels = mappedInput.mapLabels("", "Next", "", "Next");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == QuizState::SUMMARY) {
    const auto labels = mappedInput.mapLabels("Exit", "Continue", "", "");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void VocabQuizActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawHeader(renderer);

  if (state == QuizState::QUESTION) {
    drawQuestionScreen(renderer);
  } else if (state == QuizState::REVEAL) {
    drawRevealScreen(renderer);
  } else if (state == QuizState::ETYMOLOGY) {
    drawEtymologyScreen(renderer);
  } else if (state == QuizState::SUMMARY) {
    drawSummaryScreen(renderer);
  }

  drawButtonHints(renderer);
  renderer.displayBuffer();
}
