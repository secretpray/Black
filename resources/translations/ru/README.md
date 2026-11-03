# Russian game text

`strings.json` maps each string ID of BLACK's English bank (8 hex digits) to
its Russian text, or to `null` where the game's own English string is kept:
credits names, `English_UK` keys, web addresses and trademarks. It holds text
only. No game data is here: the app builds the Russian bank from the player's
own English bank and adds Cyrillic to the player's own fonts
(`ports/macos/overrides/game_translation.m`).

The text comes from an old unofficial Russian translation of the game (its
credits name MuZa-Games, voice KranX), revised for this project: 710 menu
strings corrected for meaning, spelling and consistent terms, Cyrillic
letters the old font could not draw restored, Latin restored where the
original is Latin (480P, IT, HR, EAUK, CRITERION GAMES), and names in the
credits returned to the game's own spelling. It is decoded from the old
translation's font-cell encoding into plain UTF-8.

Difficulty names follow the project's choice: НОВОБРАНЕЦ, СОЛДАТ, ВЕТЕРАН,
СПЕЦНАЗ.
