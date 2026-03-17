#include "BlackjackActivity.h"

#include <algorithm>
#include <cstring>

#include "GfxRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"

#if defined(ESP32)
#include <esp_random.h>
#endif

// Define static constexpr array
constexpr int BlackjackActivity::kBetIncrements[];

// Card dimensions for 480x800 portrait display
static constexpr int kCardW = 68;
static constexpr int kCardH = 96;
static constexpr int kCardGap = 8;
static constexpr int kCardRadius = 6;

// Layout zones (portrait 480x800, button hints at y=760)
static constexpr int kHeaderH = 40;
static constexpr int kSidePad = 24;
static constexpr int kDealerLabelY = 80;
static constexpr int kDealerCardsY = 102;
// Dealer cards bottom: 102 + 96 = 198
static constexpr int kDividerY = 370;
static constexpr int kPlayerLabelY = 395;
static constexpr int kPlayerCardsY = 417;
// Player cards bottom: 417 + 96 = 513
static constexpr int kInfoBarY = 620;

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
  lastBet = currentBet;
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

    // Left/Right to change bet increment
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      if (betIncrementIndex > 0) {
        betIncrementIndex--;
        requestUpdate();
      }
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      if (betIncrementIndex < kNumBetIncrements - 1) {
        betIncrementIndex++;
        requestUpdate();
      }
    }
    // Up = add chips to bet
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      int newBet = currentBet + increment;
      if (newBet <= chips + currentBet) {
        chips += currentBet;
        currentBet = 0;
        placeBet(newBet);
        requestUpdate();
      }
    }
    // Down = remove chips from bet
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
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

// Draw a small filled circle using fillPolygon (12-sided approximation)
static void drawFilledCircle(GfxRenderer& r, int cx, int cy, int radius) {
  constexpr int N = 12;
  int xPts[N], yPts[N];
  for (int i = 0; i < N; i++) {
    // Pre-computed sin/cos for 12 evenly spaced points (30-degree increments)
    static constexpr int sinTable[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50};
    static constexpr int cosTable[12] = {100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87};
    xPts[i] = cx + (radius * cosTable[i]) / 100;
    yPts[i] = cy + (radius * sinTable[i]) / 100;
  }
  r.fillPolygon(xPts, yPts, N, true);
}

// Draw a filled suit symbol using primitives (no font dependency)
static void drawSuitSymbol(GfxRenderer& r, int cx, int cy, int size, uint8_t suit) {
  // size = half-height of the symbol
  const int s = size;

  if (suit == 0) {
    // Spade: pointed top, rounded sides, stem
    const int xPts[] = {cx, cx + s, cx + s / 2, cx, cx - s / 2, cx - s};
    const int yPts[] = {cy - s, cy + s / 3, cy + s, cy + s / 2, cy + s, cy + s / 3};
    r.fillPolygon(xPts, yPts, 6, true);
    r.fillRect(cx - 1, cy + s / 2, 3, s / 2, true);
  } else if (suit == 1) {
    // Heart: two bumps at top, point at bottom
    const int xPts[] = {cx, cx + s, cx + s, cx + s / 2, cx, cx - s / 2, cx - s, cx - s};
    const int yPts[] = {cy + s, cy, cy - s / 2, cy - s, cy - s / 2, cy - s, cy - s / 2, cy};
    r.fillPolygon(xPts, yPts, 8, true);
  } else if (suit == 2) {
    // Diamond: rotated square
    const int xPts[] = {cx, cx + s, cx, cx - s};
    const int yPts[] = {cy - s - 2, cy, cy + s + 2, cy};
    r.fillPolygon(xPts, yPts, 4, true);
  } else if (suit == 3) {
    // Club: three circles + stem
    const int cr = s / 2 + 1;
    drawFilledCircle(r, cx, cy - s / 2, cr);
    drawFilledCircle(r, cx - s / 2, cy + s / 4, cr);
    drawFilledCircle(r, cx + s / 2, cy + s / 4, cr);
    r.fillRect(cx - 1, cy + s / 4, 3, s / 2, true);
  }
}

void BlackjackActivity::drawCard(GfxRenderer& r, int x, int y, const Card& card, bool faceDown) const {
  // Card background
  r.fillRoundedRect(x, y, kCardW, kCardH, kCardRadius, Color::White);
  r.drawRoundedRect(x, y, kCardW, kCardH, 1, kCardRadius, true);

  if (faceDown) {
    // Dithered fill for face-down card
    r.fillRectDither(x + 4, y + 4, kCardW - 8, kCardH - 8, Color::DarkGray);
  } else {
    const char* rk = rankStr(card.rank);

    // Top-left: rank text + suit symbol
    r.drawText(SMALL_FONT_ID, x + 6, y + 6, rk, true, EpdFontFamily::BOLD);
    drawSuitSymbol(r, x + 12, y + 28, 5, card.suit);

    // Center: large rank
    const int tw = r.getTextWidth(UI_12_FONT_ID, rk, EpdFontFamily::BOLD);
    r.drawText(UI_12_FONT_ID, x + (kCardW - tw) / 2, y + (kCardH - 18) / 2, rk, true, EpdFontFamily::BOLD);

    // Bottom-right: suit symbol
    drawSuitSymbol(r, x + kCardW - 12, y + kCardH - 16, 5, card.suit);
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
  const int pageW = r.getScreenWidth();

  // Separator line
  r.drawLine(kSidePad, kInfoBarY, pageW - kSidePad, kInfoBarY);

  char buf[48];
  const int y = kInfoBarY + 12;

  // Left side: bet info
  if (currentBet > 0) {
    r.drawText(SMALL_FONT_ID, kSidePad, y, "BET");
    snprintf(buf, sizeof(buf), "$%d", currentBet);
    r.drawText(UI_10_FONT_ID, kSidePad, y + 14, buf, true, EpdFontFamily::BOLD);
  }

  // Right side: record
  snprintf(buf, sizeof(buf), "%dW  %dL  %dP", wins, losses, pushes);
  const int rw = r.getTextWidth(UI_10_FONT_ID, buf);
  r.drawText(SMALL_FONT_ID, pageW - kSidePad - rw, y, "RECORD");
  r.drawText(UI_10_FONT_ID, pageW - kSidePad - rw, y + 14, buf);
}

void BlackjackActivity::drawBettingScreen(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  char buf[64];

  // Vertically centered in usable area (40 to 760)
  int y = 220;
  r.drawCenteredText(UI_10_FONT_ID, y, "PLACE YOUR BET");

  // Large bet amount
  y += 36;
  if (currentBet > 0) {
    snprintf(buf, sizeof(buf), "$%d", currentBet);
  } else {
    snprintf(buf, sizeof(buf), "$0");
  }
  r.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);

  // Increment label
  y += 60;
  r.drawCenteredText(SMALL_FONT_ID, y, "Increment");

  // Visual increment selector bar
  y += 22;
  constexpr int kPillW = 60;
  constexpr int kPillH = 28;
  constexpr int kPillGap = 10;
  const int totalW = kNumBetIncrements * kPillW + (kNumBetIncrements - 1) * kPillGap;
  const int startX = (pageW - totalW) / 2;

  for (int i = 0; i < kNumBetIncrements; i++) {
    const int px = startX + i * (kPillW + kPillGap);
    snprintf(buf, sizeof(buf), "$%d", kBetIncrements[i]);
    const int tw = r.getTextWidth(SMALL_FONT_ID, buf);
    const int textX = px + (kPillW - tw) / 2;

    if (i == betIncrementIndex) {
      r.fillRoundedRect(px, y, kPillW, kPillH, 6, Color::LightGray);
      r.drawRoundedRect(px, y, kPillW, kPillH, 2, 6, true);
      r.drawText(SMALL_FONT_ID, textX, y + 7, buf, true, EpdFontFamily::BOLD);
    } else {
      r.drawRoundedRect(px, y, kPillW, kPillH, 1, 6, true);
      r.drawText(SMALL_FONT_ID, textX, y + 7, buf);
    }
  }

  // Remaining chips
  y += kPillH + 36;
  snprintf(buf, sizeof(buf), "Remaining: $%d", chips);
  r.drawCenteredText(UI_10_FONT_ID, y, buf);

  // Record
  if (handsPlayed > 0) {
    y += 34;
    snprintf(buf, sizeof(buf), "%dW  %dL  %dP  (%d hands)", wins, losses, pushes, handsPlayed);
    r.drawCenteredText(UI_10_FONT_ID, y, buf);
  }

  if (chips <= 0) {
    y += 50;
    r.drawCenteredText(UI_12_FONT_ID, y, "Busted! Press OK to restart", true, EpdFontFamily::BOLD);
  }
}

void BlackjackActivity::drawResultMessage(GfxRenderer& r) const {
  const int pageW = r.getScreenWidth();
  const char* msg = nullptr;
  const char* detail = nullptr;
  char detailBuf[48];

  switch (result) {
    case ResultType::BLACKJACK:
      msg = "BLACKJACK!";
      snprintf(detailBuf, sizeof(detailBuf), "Won $%d  (3:2 payout)", lastBet + lastBet / 2);
      detail = detailBuf;
      break;
    case ResultType::PLAYER_WIN:
      msg = "YOU WIN!";
      snprintf(detailBuf, sizeof(detailBuf), "Won $%d", lastBet);
      detail = detailBuf;
      break;
    case ResultType::DEALER_WIN:
      msg = handValue(playerHand) > 21 ? "BUST!" : "DEALER WINS";
      snprintf(detailBuf, sizeof(detailBuf), "Lost $%d", lastBet);
      detail = detailBuf;
      break;
    case ResultType::PUSH:
      msg = "PUSH";
      detail = "Bet returned";
      break;
    default:
      return;
  }

  // Banner: rounded rect with LightGray fill, centered between dealer cards bottom and divider
  const int bannerH = 56;
  const int bannerY = (kDealerCardsY + kCardH + kDividerY) / 2 - bannerH / 2;
  const int bannerX = kSidePad;
  const int bannerW = pageW - 2 * kSidePad;

  r.fillRoundedRect(bannerX, bannerY, bannerW, bannerH, 8, Color::LightGray);
  r.drawRoundedRect(bannerX, bannerY, bannerW, bannerH, 2, 8, true);
  r.drawCenteredText(UI_12_FONT_ID, bannerY + 6, msg, true, EpdFontFamily::BOLD);
  if (detail) {
    r.drawCenteredText(SMALL_FONT_ID, bannerY + 28, detail);
  }
}

void BlackjackActivity::drawButtonHints(GfxRenderer& r) const {
  if (state == GameState::BETTING) {
    const auto labels = mappedInput.mapLabels("Exit", currentBet > 0 ? "Deal" : "", "<", ">");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawSideButtonHints(r, "+ Bet", "- Bet");
  } else if (state == GameState::PLAYER_TURN) {
    const auto labels = mappedInput.mapLabels("", "", "Hit", "Stand");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == GameState::RESULT) {
    const auto labels = mappedInput.mapLabels("Exit", "Next Hand", "", "");
    GUI.drawButtonHints(r, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}

void BlackjackActivity::render(RenderLock&&) {
  const int pageW = renderer.getScreenWidth();
  renderer.clearScreen();

  // ── Header bar (black background) ──
  renderer.fillRect(0, 0, pageW, kHeaderH, true);
  renderer.drawText(UI_12_FONT_ID, kSidePad, 12, "BLACKJACK", false, EpdFontFamily::BOLD);
  char chipBuf[16];
  snprintf(chipBuf, sizeof(chipBuf), "$%d", chips);
  const int chipW = renderer.getTextWidth(UI_12_FONT_ID, chipBuf, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, pageW - kSidePad - chipW, 12, chipBuf, false, EpdFontFamily::BOLD);

  if (state == GameState::BETTING) {
    drawBettingScreen(renderer);
  } else {
    // ── Dealer section ──
    renderer.drawText(SMALL_FONT_ID, kSidePad, kDealerLabelY, "DEALER", true, EpdFontFamily::BOLD);
    const bool hideDealerHole = (state == GameState::PLAYER_TURN);

    // Show dealer value (or "?" when hole card hidden)
    if (hideDealerHole) {
      const int qw = renderer.getTextWidth(SMALL_FONT_ID, "?", EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, pageW - kSidePad - qw, kDealerLabelY, "?", true, EpdFontFamily::BOLD);
    } else if (!dealerHand.empty()) {
      char valBuf[8];
      snprintf(valBuf, sizeof(valBuf), "%d", handValue(dealerHand));
      const int vw = renderer.getTextWidth(SMALL_FONT_ID, valBuf, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, pageW - kSidePad - vw, kDealerLabelY, valBuf, true, EpdFontFamily::BOLD);
    }

    // Center dealer hand
    const int dealerHandW = static_cast<int>(dealerHand.size()) * (kCardW + kCardGap) - kCardGap;
    const int dealerX = (pageW - dealerHandW) / 2;
    drawHand(renderer, dealerX, kDealerCardsY, dealerHand, hideDealerHole);

    // Result banner (between dealer and player)
    if (state == GameState::RESULT) {
      drawResultMessage(renderer);
    }

    // ── Divider ──
    renderer.drawLine(kSidePad, kDividerY, pageW - kSidePad, kDividerY);

    // ── Player section ──
    renderer.drawText(SMALL_FONT_ID, kSidePad, kPlayerLabelY, "YOU", true, EpdFontFamily::BOLD);

    if (!playerHand.empty()) {
      char valBuf[8];
      snprintf(valBuf, sizeof(valBuf), "%d", handValue(playerHand));
      const int vw = renderer.getTextWidth(SMALL_FONT_ID, valBuf, EpdFontFamily::BOLD);
      renderer.drawText(SMALL_FONT_ID, pageW - kSidePad - vw, kPlayerLabelY, valBuf, true, EpdFontFamily::BOLD);
    }

    // Center player hand
    const int playerHandW = static_cast<int>(playerHand.size()) * (kCardW + kCardGap) - kCardGap;
    const int playerX = (pageW - playerHandW) / 2;
    drawHand(renderer, playerX, kPlayerCardsY, playerHand);

    // ── Info bar ──
    drawChipInfo(renderer);
  }

  // ── Button hints (aligned to physical buttons) ──
  drawButtonHints(renderer);

  renderer.displayBuffer();
}
