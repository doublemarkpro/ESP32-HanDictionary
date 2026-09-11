#pragma once

namespace han {
struct Sound {
    const char* id;
    const char* ipa;
    const char* words[3];
    int category;
};
// A small reviewed starter set. Counts describe this pack, not all English phonemes.
inline constexpr Sound kSounds[] = {
    {"i-long", "iː", {"sheep", "tree", "tea"}, 0}, {"i-short", "ɪ", {"fish", "ship", "sit"}, 0},
    {"e", "e", {"bed", "pen", "red"}, 0},          {"ae", "æ", {"cat", "hat", "bag"}, 0},
    {"wedge", "ʌ", {"cup", "sun", "bus"}, 0},      {"a-long", "ɑː", {"car", "star", "park"}, 0},
    {"ei", "eɪ", {"day", "rain", "cake"}, 1},      {"ai", "aɪ", {"bike", "kite", "five"}, 1},
    {"oi", "ɔɪ", {"boy", "toy", "coin"}, 1},       {"au", "aʊ", {"cow", "house", "mouse"}, 1},
    {"ou", "əʊ", {"boat", "go", "home"}, 1},       {"p", "p", {"pen", "pig", "cap"}, 2},
    {"b", "b", {"bag", "bed", "bus"}, 2},          {"t", "t", {"tea", "top", "cat"}, 2},
    {"d", "d", {"dog", "day", "bed"}, 2},          {"f", "f", {"fish", "fan", "leaf"}, 2},
    {"v", "v", {"van", "vet", "five"}, 2},
};
}  // namespace han
