#include "BlackjackActivity.h"

#include <algorithm>
#include <cstring>

#include "GfxRenderer.h"
#include "fontIds.h"

#if defined(ESP32)
#include <esp_random.h>
#endif

// Define static constexpr array
constexpr int BlackjackActivity::kBetIncrements[];

// Card dimensions for 800x480 display
static constexpr int kCardW = 70;
static constexpr int kCardH = 100;
static constexpr int kCardGap = 8;
static constexpr int kCardRadius = 6;
static constexpr int kCardBorder = 1;

// Layout zones
static constexpr int kDealerY = 40;
static constexpr int kPlayerY = 260;
static constexpr int kHandStartX = 60;
static constexpr int kChipInfoX = 620;

void BlackjackActivity::onEnter() {
  chips = kStartingChips;
  currentBet = 0;
  wins = 0;
  losses = 0;
  pushes = 0;
  handsPlayed = 0;
  betIncrementIndex = 2;  // 50
  state = GameState::BETTING;
  initDeck();
  shuffleDeck();
  requestUpdate();
}

void BlackjackActivity::onExit() {}

// ── Deck Management ──────────────────────────────────────────────

void BlackjackActivity::initDeck() {
  for (int i = 0; i < 52; i++) {
    deck[i] = {static_cast<uint8_t>(i % 13), static_cast<uint8_t>(i / 13)};
  }
  deckIndex = 0;
}

void BlackjackActivity::shuffleDeck() {
  // Fisher-Yates shuffle
  for (int i = 51; i > 0; i--) {
#if defined(ESP32)
    int j = esp_random() % (i + 1);
#else
    int j = rand() % (i + 1);
#endif
    std::swap(deck[i], deck[j]);
  }
  deckIndex = 0;
}

BlackjackActivity::Card BlackjackActivity::drawCard() {
  if (deckIndex >= 52) {
    initDeck();
    shuffleDeck();
  }
  return deck[deckIndex++];
}

// ── Hand Evaluation ──────────────────────────────────────────────

int BlackjackActivity::handValue(const std::vector<Card>& hand) const {
  int value = 0;
  int aces = 0;
  for (const auto& c : hand) {
    if (c.rank == 0) {
      aces++;
      value += 11;
    } else if (c.rank >= 10) {
      value += 10;
    } else {
      value += c.rank + 1;
    }
  }
  while (value > 21 && aces > 0) {
    value -= 10;
    aces--;
  }
  return value;
}

bool BlackjackActivity::isSoft(const std::vector<Card>& hand) const {
  int value = 0;
  int aces = 0;
  for (const auto& c : hand) {
    if (c.rank == 0) {
      aces++;
      value += 11;
    } else if (c.rank >= 10) {
      value += 10;
    } else {
      value += c.rank + 1;
    }
  }
  int reduced = 0;
  while (value > 21 && reduced < aces) {
    value -= 10;
    reduced++;
  }
  return reduced < aces;
}

// ── Betting ──────────────────────────────────────────────────────

void BlackjackActivity::placeBet(int amount) {
  if (amount > 0 && amount <= chips) {
    currentBet = amount;
    chips -= currentBet;
  }
}

// ── Game Logic ───────────────────────────────────────────────────

void BlackjackActivity::dealInitialCards() {
  playerHand.clear();
  dealerHand.clear();
  result = ResultType::NONE;

  // Reshuffle if low on cards
  if (deckIndex > 40) {
    shuffleDeck();
  }

  playerHand.push_back(drawCard());
  dealerHand.push_back(drawCard());
  playerHand.push_back(drawCard());
  dealerHand.push_back(drawCard());

  handsPlayed++;

  // Check for natural blackjack
  if (handValue(playerHand) == 21) {
    state = GameState::RESULT;
    if (handValue(dealerHand) == 21) {
      result = ResultType::PUSH;
      pushes++;
    } else {
      result = ResultType::BLACKJACK;
      wins++;
    }
    payout();
  } else {
    state = GameState::PLAYER_TURN;
  }
}

void BlackjackActivity::playerHit() {
  playerHand.push_back(drawCard());
  if (handValue(playerHand) > 21) {
    state = GameState::RESULT;
    result = ResultType::DEALER_WIN;
    losses++;
    payout();
  }
}

void BlackjackActivity::dealerPlay() {
  state = GameState::DEALER_TURN;
  while (handValue(dealerHand) < 17 || (handValue(dealerHand) == 17 && isSoft(dealerHand))) {
    dealerHand.push_back(drawCard());
  }
  resolveRound();
}

void BlackjackActivity::resolveRound() {
  state = GameState::RESULT;
  int pv = handValue(playerHand);
  int dv = handValue(dealerHand);

  if (dv > 21) {
    result = ResultType::PLAYER_WIN;
    wins++;
  } else if (pv > dv) {
    result = ResultType::PLAYER_WIN;
    wins++;
  } else if (dv > pv) {
    result = ResultType::DEALER_WIN;
    losses++;
  } else {
    result = ResultType::PUSH;
    pushes++;
  }
  payout();
}

void BlackjackActivity::payout() {
  switch (result) {
    case ResultType::BLACKJACK:
      chips += currentBet + (currentBet * 3 / 2);  // 3:2 payout
      break;
    case ResultType::PLAYER_WIN:
      chips += currentBet * 2;  // 1:1 payout (bet + winnings)
      break;
    case ResultType::PUSH:
      chips += currentBet;  // return bet
      break;
    case ResultType::DEALER_WIN:
      // bet already deducted
      break;
    default:
      break;
  }
  currentBet = 0;
}

// ── Input Handling ───────────────────────────────────────────────

void BlackjackActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (state == GameState::BETTING) {
    const int increment = kBetIncrements[betIncrementIndex];

    // Up/Down to change bet increment
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (betIncrementIndex < kNumBetIncrements - 1) {
        betIncrementIndex++;
        requestUpdate();
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (betIncrementIndex > 0) {
        betIncrementIndex--;
        requestUpdate();
      }
    }
    // Right = add chips to bet
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      int newBet = currentBet + increment;
      if (newBet <= chips + currentBet) {  // can afford it
        // Return current bet to chips, then place new bet
        chips += currentBet;
        currentBet = 0;
        placeBet(newBet);
        requestUpdate();
      }
    }
    // Left = remove chips from bet
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (currentBet > 0) {
        int newBet = std::max(0, currentBet - increment);
        chips += currentBet;
        currentBet = 0;
        if (newBet > 0) placeBet(newBet);
        requestUpdate();
      }
    }
    // Confirm = deal (if bet placed)
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (currentBet > 0) {
        dealInitialCards();
        requestUpdate();
      }
    }
  } else if (state == GameState::PLAYER_TURN) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      playerHit();
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      dealerPlay();
      requestUpdate();
    }
  } else if (state == GameState::RESULT) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (chips <= 0) {
        // Busted — reset
        chips = kStartingChips;
        wins = 0;
        losses = 0;
        pushes = 0;
        handsPlayed = 0;
      }
      state = GameState::BETTING;
      requestUpdate();
    }
  }
}

// ── Rendering ────────────────────────────────────────────────────

const char* BlackjackActivity::rankStr(uint8_t rank) {
  static const char* ranks[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
  return (rank < 13) ? ranks[rank] : "?";
}

const char* BlackjackActivity::suitStr(uint8_t suit) {
  static const char* suits[] = {"S", "H", "D", "C"};
  return (suit < 4) ? suits[suit] : "?";
}

void BlackjackActivity::drawCard(GfxRenderer& r, int x, int y, const Card& card, bool faceDown) const {
  r.fillRoundedRect(x, y, kCardW, kCardH, kCardRadius, Color::White);

  if (faceDown) {
    r.drawRoundedRect(x, y, kCardW, kCardH, kCardBorder, kCardRadius, true);
    r.fillRectDither(x + 4, y + 4, kCardW - 8, kCardH - 8, Color::DarkGray);
  } else {
    r.drawRoundedRect(x, y, kCardW, kCardH, kCardBorder, kCardRadius, true);

    const char* rk = rankStr(card.rank);
    r.drawText(UI_12_FONT_ID, x + 6, y + 6, rk, true, EpdFontFamily::BOLD);

    const char* st = suitStr(card.suit);
    r.drawText(UI_10_FONT_ID, x + 6, y + 28, st);

    const int tw = r.getTextWidth(UI_12_FONT_ID, rk, EpdFontFamily::BOLD);
    r.drawText(UI_12_FONT_ID, x + (kCardW - tw) / 2, y + (kCardH - 20) / 2, rk, true, EpdFontFamily::BOLD);

    const int stw = r.getTextWidth(UI_10_FONT_ID, st);
    r.drawText(UI_10_FONT_ID, x + kCardW - stw - 6, y + kCardH - 22, st);
  }
}

void BlackjackActivity::drawHand(GfxRenderer& r, int x, int y, const std::vector<Card>& hand,
                                  bool hideFirst) const {
  for (size_t i = 0; i < hand.size(); i++) {
    int cx = x + static_cast<int>(i) * (kCardW + kCardGap);
    drawCard(r, cx, y, hand[i], hideFirst && i == 0);
  }
}

void BlackjackActivity::drawHandValue(GfxRenderer& r, int x, int y, const std::vector<Card>& hand) const {
  int val = handValue(hand);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", val);
  r.drawText(UI_12_FONT_ID, x, y, buf, true, EpdFontFamily::BOLD);
}

void BlackjackActivity::drawChipInfo(GfxRenderer& r) const {
  char buf[48];

  r.drawText(UI_12_FONT_ID, kChipInfoX, 50, "Chips", true, EpdFontFamily::BOLD);
  snprintf(buf, sizeof(buf), "%d", chips);
  r.drawText(UI_12_FONT_ID, kChipInfoX, 75, buf);

  if (currentBet > 0) {
    r.drawText(UI_10_FONT_ID, kChipInfoX, 105, "Bet");
    snprintf(buf, sizeof(buf), "%d", currentBet);
    r.drawText(UI_12_FONT_ID, kChipInfoX, 125, buf, true, EpdFontFamily::BOLD);
  }

  // Win/Loss record
  r.drawLine(kChipInfoX, 155, kChipInfoX + 120, 155);
  snprintf(buf, sizeof(buf), "%dW  %dL  %dP", wins, losses, pushes);
  r.drawText(UI_10_FONT_ID, kChipInfoX, 165, buf);
  snprintf(buf, sizeof(buf), "Hands: %d", handsPlayed);
  r.drawText(UI_10_FONT_ID, kChipInfoX, 185, buf);
}

void BlackjackActivity::drawBettingScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  const int centerX = pageW / 2;

  // Current chip stack
  char buf[64];
  snprintf(buf, sizeof(buf), "Chips: %d", chips);
  r.drawCenteredText(UI_12_FONT_ID, 80, buf, true, EpdFontFamily::BOLD);

  // Current bet
  if (currentBet > 0) {
    snprintf(buf, sizeof(buf), "Current Bet: %d", currentBet);
  } else {
    snprintf(buf, sizeof(buf), "Place your bet");
  }
  r.drawCenteredText(UI_12_FONT_ID, 140, buf);

  // Bet increment selector
  const int increment = kBetIncrements[betIncrementIndex];
  snprintf(buf, sizeof(buf), "Increment: %d", increment);
  r.drawCenteredText(UI_10_FONT_ID, 190, buf);

  // Visual increment selector
  const int selectorY = 220;
  for (int i = 0; i < kNumBetIncrements; i++) {
    snprintf(buf, sizeof(buf), "%d", kBetIncrements[i]);
    const int tw = r.getTextWidth(UI_10_FONT_ID, buf);
    const int bx = centerX - 150 + i * 75;
    if (i == betIncrementIndex) {
      r.fillRoundedRect(bx - 5, selectorY - 3, tw + 10, 22, 4, Color::LightGray);
      r.drawRoundedRect(bx - 5, selectorY - 3, tw + 10, 22, 2, 4, true);
    }
    r.drawText(UI_10_FONT_ID, bx, selectorY, buf);
  }

  // Record from previous session
  if (handsPlayed > 0) {
    snprintf(buf, sizeof(buf), "%dW  %dL  %dP  (%d hands)", wins, losses, pushes, handsPlayed);
    r.drawCenteredText(UI_10_FONT_ID, 290, buf);
  }

  if (chips <= 0) {
    r.drawCenteredText(UI_12_FONT_ID, 340, "Busted! Press OK to restart", true, EpdFontFamily::BOLD);
  }
}

void BlackjackActivity::drawResultMessage(GfxRenderer& r) const {
  const char* msg = nullptr;
  switch (result) {
    case ResultType::BLACKJACK:
      msg = "BLACKJACK!";
      break;
    case ResultType::PLAYER_WIN:
      msg = "You Win!";
      break;
    case ResultType::DEALER_WIN:
      msg = "Dealer Wins";
      break;
    case ResultType::PUSH:
      msg = "Push";
      break;
    default:
      return;
  }

  const int msgY = kDealerY + kCardH + 30;
  r.drawCenteredText(UI_12_FONT_ID, msgY, msg, true, EpdFontFamily::BOLD);

  // Show payout info
  char buf[48];
  switch (result) {
    case ResultType::BLACKJACK: {
      int payed = currentBet > 0 ? currentBet : (chips - kStartingChips);  // already paid out
      snprintf(buf, sizeof(buf), "+%d (3:2)", payed);
      break;
    }
    case ResultType::PLAYER_WIN:
      snprintf(buf, sizeof(buf), "Won bet");
      break;
    case ResultType::DEALER_WIN:
      snprintf(buf, sizeof(buf), "Lost bet");
      break;
    case ResultType::PUSH:
      snprintf(buf, sizeof(buf), "Bet returned");
      break;
    default:
      return;
  }
  r.drawCenteredText(UI_10_FONT_ID, msgY + 25, buf);
}

void BlackjackActivity::drawButtonHints(GfxRenderer& r) const {
  const int pageH = r.getScreenHeight();
  const int hintY = pageH - 25;

  if (state == GameState::BETTING) {
    r.drawText(UI_10_FONT_ID, 20, hintY, "< - Bet");
    r.drawText(UI_10_FONT_ID, 180, hintY, "+ Bet >");
    r.drawText(UI_10_FONT_ID, 360, hintY, "Deal [OK]");
    r.drawText(UI_10_FONT_ID, 650, hintY, "Back");
  } else if (state == GameState::PLAYER_TURN) {
    r.drawText(UI_10_FONT_ID, 30, hintY, "< Hit");
    r.drawText(UI_10_FONT_ID, 350, hintY, "Stand >");
    r.drawText(UI_10_FONT_ID, 650, hintY, "Back");
  } else if (state == GameState::RESULT) {
    r.drawCenteredText(UI_10_FONT_ID, hintY, "Press OK for next hand  |  Back to exit");
  }
}

void BlackjackActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Title
  renderer.drawCenteredText(UI_12_FONT_ID, 8, "Blackjack", true, EpdFontFamily::BOLD);
  renderer.drawLine(0, 30, renderer.getScreenWidth(), 30);

  if (state == GameState::BETTING) {
    drawBettingScreen(renderer);
  } else {
    // Dealer label + hand
    renderer.drawText(UI_10_FONT_ID, kHandStartX, kDealerY, "Dealer");
    const bool hideDealerHole = (state == GameState::PLAYER_TURN);
    drawHand(renderer, kHandStartX, kDealerY + 20, dealerHand, hideDealerHole);

    if (!hideDealerHole && !dealerHand.empty()) {
      drawHandValue(renderer, kHandStartX + static_cast<int>(dealerHand.size()) * (kCardW + kCardGap) + 10,
                    kDealerY + 60, dealerHand);
    }

    // Player label + hand
    renderer.drawText(UI_10_FONT_ID, kHandStartX, kPlayerY, "You");
    drawHand(renderer, kHandStartX, kPlayerY + 20, playerHand);

    if (!playerHand.empty()) {
      drawHandValue(renderer, kHandStartX + static_cast<int>(playerHand.size()) * (kCardW + kCardGap) + 10,
                    kPlayerY + 60, playerHand);
    }

    // Chip info sidebar
    drawChipInfo(renderer);

    // Result message
    if (state == GameState::RESULT) {
      drawResultMessage(renderer);
    }
  }

  // Button hints
  drawButtonHints(renderer);

  renderer.displayBuffer();
}
