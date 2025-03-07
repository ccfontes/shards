/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright © 2019 Fragcolor Pte. Ltd. */
/* Copyright (C) 2015 Joel Martin <github@martintribe.org> */

#include "mal/MAL.h"
#if defined(__clang__) && defined(_WIN32)
#define _REGEX_MAX_STACK_COUNT 200000
#endif
#include <regex>

#include "MAL.h"
#include "Types.h"

#include <iostream>

typedef std::regex Regex;

static const Regex intRegex("^[-+]?\\d+$");
static const Regex hexRegex("^0x[0-9a-fA-F]+$");
static const Regex floatRegex("^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");
static const Regex closeRegex("[\\)\\]}]");

static const Regex newlineRegex("(\\r\\n|\\r|\\n)");
static const Regex whitespaceRegex("[\\s,]+|;.*");
static const Regex tokenRegexes[] = {
    Regex("~@"),
    Regex("#\\("),
    Regex("[\\[\\]{}()'`~^@]"),
    Regex("\"(?:\\\\.|[^\\\\\"])*\""),
    Regex("#\"(?:\\\\.|[^\\\\\"])*\""),
    Regex("[^\\s\\[\\]{}('\"`,;)]+"),
};

class Tokeniser {
public:
  Tokeniser(const MalString &input);

  MalString peek() const {
    ASSERT(!eof(), "Tokeniser reading past EOF in peek\n");
    return m_token;
  }

  MalString next() {
    ASSERT(!eof(), "Tokeniser reading past EOF in next\n");
    MalString ret = peek();
    nextToken();
    return ret;
  }

  bool eof() const { return m_iter == m_end; }

private:
  void skipWhitespace();
  void nextToken();

  bool matchRegex(const Regex &regex);

  typedef MalString::const_iterator StringIter;

  MalString m_token;
  StringIter m_iter;
  StringIter m_begin;
  StringIter m_end;
};

Tokeniser::Tokeniser(const MalString &input) : m_iter(input.begin()), m_begin(input.begin()), m_end(input.end()) { nextToken(); }

bool Tokeniser::matchRegex(const Regex &regex) {
  if (eof()) {
    return false;
  }

  std::smatch match;
  auto flags = std::regex_constants::match_continuous;
  if (!std::regex_search(m_iter, m_end, match, regex, flags)) {
    return false;
  }

  ASSERT(match.size() == 1, "Should only have one submatch, not %lu\n", match.size());
  ASSERT(match.position(0) == 0, "Need to match first character\n");
  ASSERT(match.length(0) > 0, "Need to match a non-empty string\n");

  // Don't advance  m_iter now, do it after we've consumed the token in
  // next().  If we do it now, we hit eof() when there's still one token left.
  m_token = match.str(0);

  return true;
}

void Tokeniser::nextToken() {
  m_iter += m_token.size();

  skipWhitespace();
  if (eof()) {
    return;
  }

  m_begin = m_iter;

  for (auto &it : tokenRegexes) {
    if (matchRegex(it)) {
      return;
    }
  }

  MalString mismatch(m_iter, m_end);
  if (mismatch[0] == '"') {
    MAL_CHECK(false, "expected '\"', got EOF");
  } else {
    MAL_CHECK(false, "unexpected '%s'", mismatch.c_str());
  }
}

void Tokeniser::skipWhitespace() {
  while (matchRegex(whitespaceRegex)) {
    m_iter += m_token.size();
  }
}

#define VALUE_WITH_LINE(__val__)  \
  [&]() {                         \
    auto val = __val__;           \
    val->line = tokeniser.line(); \
    return val;                   \
  }()

static malValuePtr readAtom(Tokeniser &tokeniser);
static malValuePtr readForm(Tokeniser &tokeniser);
static void readList(Tokeniser &tokeniser, malValueVec *items, const MalString &end);
static malValuePtr processMacro(Tokeniser &tokeniser, const MalString &symbol);

malValuePtr readStr(const MalString &input) {
  Tokeniser tokeniser(input);
  if (tokeniser.eof()) {
    throw malEmptyInputException();
  }
  return readForm(tokeniser);
}

static malValuePtr readForm(Tokeniser &tokeniser) {
  MAL_CHECK(!tokeniser.eof(), "expected form, got EOF");
  MalString token = tokeniser.peek();

  MAL_CHECK(!std::regex_match(token, closeRegex), "unexpected '%s'", token.c_str());

  if (token == "(") {
    tokeniser.next();
    std::unique_ptr<malValueVec> items(new malValueVec);
    readList(tokeniser, items.get(), ")");
    return mal::list(items.release());
  } else if (token == "[") {
    tokeniser.next();
    std::unique_ptr<malValueVec> items(new malValueVec);
    readList(tokeniser, items.get(), "]");
    return mal::vector(items.release());
  } else if (token == "#(") {
    tokeniser.next();
    std::unique_ptr<malValueVec> items(new malValueVec);
    readList(tokeniser, items.get(), ")");
    return mal::list(mal::symbol("wireify"), mal::vector(items.release()));
  } else if (token == "{") {
    tokeniser.next();
    malValueVec items;
    readList(tokeniser, &items, "}");
    return mal::hash(items.begin(), items.end(), false);
  } else {
    return readAtom(tokeniser);
  }
}

static malValuePtr convertToType(malValuePtr value) { return value; }

static malValuePtr readAtom(Tokeniser &tokeniser) {
  struct ReaderMacro {
    const char *token;
    const char *symbol;
  };
  ReaderMacro macroTable[] = {{"@", "deref"}, {"`", "quasiquote"}, {"'", "quote"}, {"~@", "splice-unquote"}, {"~", "unquote"}};

  struct Constant {
    const char *token;
    malValuePtr value;
  };
  Constant constantTable[] = {
      {"false", mal::falseValue()},
      {"nil", mal::nilValue()},
      {"true", mal::trueValue()},
  };

  MalString token = tokeniser.next();

  if (token[0] == '"') {
    return mal::string(unescape(token));
  }

  if (token[0] == '#' && token[1] == '"') {
    return mal::string(token.substr(2, token.length() - 3));
  }

  if (token[0] == ':') {
    return mal::keyword(token);
  }

  if (token[0] == '.') {
    auto str = token.substr(1);
    return mal::contextVar(str);
  }

  if (token == "^") {
    malValuePtr meta = readForm(tokeniser);
    malValuePtr value = readForm(tokeniser);
    // Note that meta and value switch places
    return mal::list(mal::symbol("with-meta"), value, meta);
  }

  for (auto &constant : constantTable) {
    if (token == constant.token) {
      return constant.value;
    }
  }
  for (auto &macro : macroTable) {
    if (token == macro.token) {
      return processMacro(tokeniser, macro.symbol);
    }
  }

  if (std::regex_match(token, intRegex)) {
    return mal::number(token, true);
  }

  if (std::regex_match(token, floatRegex)) {
    return mal::number(token, false);
  }

  if (std::regex_match(token, hexRegex)) {
    return mal::numberHex(token);
  }

  return mal::symbol(token);
}

static void readList(Tokeniser &tokeniser, malValueVec *items, const MalString &end) {
  while (1) {
    MAL_CHECK(!tokeniser.eof(), "expected '%s', got EOF", end.c_str());
    if (tokeniser.peek() == end) {
      tokeniser.next();
      return;
    }
    items->push_back(readForm(tokeniser));
  }
}

static malValuePtr processMacro(Tokeniser &tokeniser, const MalString &symbol) {
  return mal::list(mal::symbol(symbol), readForm(tokeniser));
}
