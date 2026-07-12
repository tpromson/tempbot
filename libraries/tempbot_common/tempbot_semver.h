#ifndef TEMPBOT_SEMVER_H
#define TEMPBOT_SEMVER_H

// Arduino-independent implementation shared by firmware and host tests.
struct TempBotSemver {
  unsigned long major;
  unsigned long minor;
  unsigned long patch;
};

inline bool tempbotSemverSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline unsigned long tempbotParseSegment(const char*& input) {
  while (tempbotSemverSpace(*input)) input++;
  unsigned long value = 0;
  while (*input >= '0' && *input <= '9') {
    value = value * 10 + static_cast<unsigned long>(*input - '0');
    input++;
  }
  while (*input && *input != '.') input++;
  if (*input == '.') input++;
  return value;
}

inline TempBotSemver tempbotParseSemver(const char* value) {
  if (!value) return {0, 0, 0};
  const char* input = value;
  return {tempbotParseSegment(input), tempbotParseSegment(input), tempbotParseSegment(input)};
}

inline bool tempbotIsNewerVersion(const char* latest, const char* current) {
  TempBotSemver l = tempbotParseSemver(latest);
  TempBotSemver c = tempbotParseSemver(current);
  if (l.major != c.major) return l.major > c.major;
  if (l.minor != c.minor) return l.minor > c.minor;
  return l.patch > c.patch;
}

#endif
