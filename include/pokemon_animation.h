#ifndef GUARD_POKEMON_ANIMATION_H
#define GUARD_POKEMON_ANIMATION_H

// Pokémon back animation sets
enum BackAnim
{
    BACK_ANIM_NONE,
};

// Pokémon animation function ids (for front and back)
// Each front anim uses 1, and each back anim uses a set of 3
enum AnimFunctionIDs
{
    ANIM_NONE,
    ANIM_COUNT,
};

enum BackAnim GetSpeciesBackAnimSet(enum Species species);
void LaunchAnimationTaskForFrontSprite(struct Sprite *sprite, enum AnimFunctionIDs frontAnimId);
void StartMonSummaryAnimation(struct Sprite *sprite, enum AnimFunctionIDs frontAnimId);
void LaunchAnimationTaskForBackSprite(struct Sprite *sprite, enum BackAnim backAnimSet);
void SetSpriteCB_MonAnimDummy(struct Sprite *sprite);

#endif // GUARD_POKEMON_ANIMATION_H