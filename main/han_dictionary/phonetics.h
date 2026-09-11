#pragma once
namespace han {
struct Sound {
    const char* id;
    const char* ipa;
    const char* words[3];
    int category;
};
// Traditional British teaching inventory: 12 monophthongs, 8 diphthongs, 24 consonants.
// Accents differ; this is a teaching convention, not a universal phoneme count.
inline constexpr Sound kSounds[] = {
    {"i-long", "iː", {"sheep", "tree", "tea"}, 0},
    {"i-short", "ɪ", {"fish", "ship", "sit"}, 0},
    {"e", "e", {"bed", "pen", "red"}, 0},
    {"ae", "æ", {"cat", "hat", "bag"}, 0},
    {"wedge", "ʌ", {"cup", "sun", "bus"}, 0},
    {"a-long", "ɑː", {"car", "star", "park"}, 0},
    {"o-short", "ɒ", {"hot", "dog", "box"}, 0},
    {"o-long", "ɔː", {"saw", "ball", "door"}, 0},
    {"u-short", "ʊ", {"book", "foot", "good"}, 0},
    {"u-long", "uː", {"blue", "food", "moon"}, 0},
    {"er-long", "ɜː", {"bird", "nurse", "turn"}, 0},
    {"schwa", "ə", {"about", "sofa", "banana"}, 0},
    {"ei", "eɪ", {"day", "rain", "cake"}, 1},
    {"ai", "aɪ", {"bike", "kite", "five"}, 1},
    {"oi", "ɔɪ", {"boy", "toy", "coin"}, 1},
    {"au", "aʊ", {"cow", "house", "mouse"}, 1},
    {"ou", "əʊ", {"boat", "go", "home"}, 1},
    {"ia", "ɪə", {"ear", "near", "deer"}, 1},
    {"ea", "eə", {"air", "hair", "chair"}, 1},
    {"ua", "ʊə", {"pure", "cure", "tour"}, 1},
    {"p", "p", {"pen", "pig", "cap"}, 2},
    {"b", "b", {"bag", "bed", "bus"}, 2},
    {"t", "t", {"tea", "top", "cat"}, 2},
    {"d", "d", {"dog", "day", "bed"}, 2},
    {"k", "k", {"cat", "key", "back"}, 2},
    {"g", "ɡ", {"go", "game", "bag"}, 2},
    {"f", "f", {"fish", "fan", "leaf"}, 2},
    {"v", "v", {"van", "vet", "five"}, 2},
    {"th", "θ", {"thin", "think", "bath"}, 2},
    {"dh", "ð", {"this", "that", "mother"}, 2},
    {"s", "s", {"sun", "sit", "bus"}, 2},
    {"z", "z", {"zoo", "zip", "nose"}, 2},
    {"sh", "ʃ", {"sheep", "shoe", "fish"}, 2},
    {"zh", "ʒ", {"vision", "measure", "usual"}, 2},
    {"h", "h", {"hat", "hand", "home"}, 2},
    {"ch", "tʃ", {"chair", "cheese", "watch"}, 2},
    {"jh", "dʒ", {"jam", "juice", "bridge"}, 2},
    {"m", "m", {"moon", "milk", "room"}, 2},
    {"n", "n", {"nose", "net", "sun"}, 2},
    {"ng", "ŋ", {"sing", "ring", "long"}, 2},
    {"l", "l", {"leg", "leaf", "ball"}, 2},
    {"r", "r", {"red", "rain", "room"}, 2},
    {"w", "w", {"wet", "win", "water"}, 2},
    {"y", "j", {"yes", "yellow", "young"}, 2},
};
inline constexpr int SoundCount(int category) {
    int count = 0;
    for (const auto& sound : kSounds)
        if (sound.category == category)
            ++count;
    return count;
}
inline constexpr int SoundAt(int category, int offset) {
    for (int i = 0; i < 44; ++i)
        if (kSounds[i].category == category && offset-- == 0)
            return i;
    return -1;
}
static_assert(SoundCount(0) == 12 && SoundCount(1) == 8 && SoundCount(2) == 24);
}  // namespace han
