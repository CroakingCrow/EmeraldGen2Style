#include "global.h"
#include "battle.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_animation.h"
#include "sprite.h"
#include "task.h"
#include "test/battle.h"
#include "test_runner.h"
#include "trig.h"
#include "util.h"
#include "data.h"
#include "constants/battle_anim.h"
#include "constants/rgb.h"

/*
    This file handles the movements of the Pokémon intro animations.

    Each animation type is identified by an ANIM_* constant that
    refers to a sprite callback to start the animation. These functions
    are named Anim_<name> or Anim_<name>_<variant>. Many of these
    functions share additional movement functions to do a variation of the
    same movement (e.g. a faster or larger movement).
    Vertical and Horizontal are frequently shortened to V and H.

    Every front animation uses 1 of these ANIMs, and every back animation
    uses a BACK_ANIM_* that refers to a set of 3 ANIM functions. Which of the
    3 that gets used depends on the Pokémon's nature (see sBackAnimationIds).

    The gSpeciesInfo table links to both BACK_ANIM and ANIM in its frontAnimId and backAnimId fields.

    These are the functions that will start an animation:
    - LaunchAnimationTaskForFrontSprite
    - LaunchAnimationTaskForBackSprite
    - StartMonSummaryAnimation
*/

#define sDontFlip data[1]  // TRUE if a normal animation, FALSE if Summary Screen animation

struct PokemonAnimData
{
    u16 delay;
    s16 speed; // Only used by 2 sets of animations
    s16 runs; // Number of times to do the animation
    s16 rotation;
    s16 data; // General use
};

struct YellowFlashData
{
    bool8 isYellow;
    u8 time;
};

static void Anim_None(struct Sprite *sprite);

static void WaitAnimEnd(struct Sprite *sprite);

static struct PokemonAnimData sAnims[MAX_BATTLERS_COUNT];
static u8 sAnimIdx;
static bool32 sIsSummaryAnim;

static const u8 sSpeciesToBackAnimSet[] =
{
};

// Equivalent to struct YellowFlashData, but doesn't match as a struct
static const u8 sYellowFlashData[][2] =
{
    {FALSE,  5},
    { TRUE,  1},
    {FALSE, 15},
    { TRUE,  4},
    {FALSE,  2},
    { TRUE,  2},
    {FALSE,  2},
    { TRUE,  2},
    {FALSE,  2},
    { TRUE,  2},
    {FALSE,  2},
    { TRUE,  2},
    {FALSE,  2},
    {FALSE, -1}
};

static const u8 sVerticalShakeData[][2] =
{
    { 6,  30},
    {-2,  15},
    { 6,  30},
    {-1,   0}
};

static void (*const sMonAnimFunctions[])(struct Sprite *sprite) =
{
    [ANIM_NONE]                         = Anim_None,
};

// Each back anim set has 3 possible animations depending on nature
// Each of the 3 animations is a slight variation of the others
// BACK_ANIM_NONE is skipped below. GetSpeciesBackAnimSet subtracts 1 from the back anim id
static const u8 sBackAnimationIds[] =
{
  [BACK_ANIM_NONE]                     = ANIM_NONE,
};

static const union AffineAnimCmd sMonAffineAnim_0[] =
{
    AFFINEANIMCMD_FRAME(256, 256, 0, 0),
    {AFFINEANIMCMDTYPE_END}
};

static const union AffineAnimCmd sMonAffineAnim_1[] =
{
    AFFINEANIMCMD_FRAME(-256, 256, 0, 0),
    {AFFINEANIMCMDTYPE_END}
};

static const union AffineAnimCmd *const sMonAffineAnims[] =
{
    sMonAffineAnim_0,
    sMonAffineAnim_1
};

static void MonAnimDummySpriteCallback(struct Sprite *sprite)
{
}

static void SetPosForRotation(struct Sprite *sprite, u16 index, s16 amplitudeX, s16 amplitudeY)
{
    s16 xAdder, yAdder;

    amplitudeX *= -1;
    amplitudeY *= -1;

    xAdder = Cos(index, amplitudeX) - Sin(index, amplitudeY);
    yAdder = Cos(index, amplitudeY) + Sin(index, amplitudeX);

    amplitudeX *= -1;
    amplitudeY *= -1;

    sprite->x2 = xAdder + amplitudeX;
    sprite->y2 = yAdder + amplitudeY;
}

enum BackAnim GetSpeciesBackAnimSet(enum Species species)
{
    if (gSpeciesInfo[species].backAnimId != BACK_ANIM_NONE)
        return gSpeciesInfo[species].backAnimId - 1;
    else
        return BACK_ANIM_NONE;
}

#define tState  data[0]
#define tPtrHi  data[1]
#define tPtrLo  data[2]
#define tAnimId data[3]
#define tBattlerId data[4]
#define tSpeciesId data[5]

// BUG: In vanilla, tPtrLo is read as an s16, so if bit 15 of the
// address were to be set it would cause the pointer to be read
// as 0xFFFFXXXX instead of the desired 0x02YYXXXX.
// By dumb luck, this is not an issue in vanilla. However,
// changing the link order revealed this bug.
#if MODERN || defined(BUGFIX)
#define ANIM_SPRITE(taskId)   ((struct Sprite *)((gTasks[taskId].tPtrHi << 16) | ((u16)gTasks[taskId].tPtrLo)))
#else
#define ANIM_SPRITE(taskId)   ((struct Sprite *)((gTasks[taskId].tPtrHi << 16) | (gTasks[taskId].tPtrLo)))
#endif //MODERN || BUGFIX

static void Task_HandleMonAnimation(u8 taskId)
{
    u32 i;
    struct Sprite *sprite = ANIM_SPRITE(taskId);

    if (gTasks[taskId].tState == 0)
    {
        gTasks[taskId].tBattlerId = sprite->oam.paletteNum;
        gTasks[taskId].tSpeciesId = sprite->data[2];
        sprite->sDontFlip = TRUE;
        sprite->data[0] = 0;

        for (i = 2; i < ARRAY_COUNT(sprite->data); i++)
            sprite->data[i] = 0;

            if (gTestRunnerHeadless && !gBattleTestRunnerState->forceMoveAnim)
            sprite->callback = WaitAnimEnd;
        else
            sprite->callback = sMonAnimFunctions[gTasks[taskId].tAnimId];
        sIsSummaryAnim = FALSE;

        gTasks[taskId].tState++;
    }
    if (sprite->callback == SpriteCallbackDummy)
    {
        sprite->data[0] = gTasks[taskId].tBattlerId;
        sprite->data[2] = gTasks[taskId].tSpeciesId;
        sprite->data[1] = 0;

        // Task_HandleMonAnimation handles more than just KO animations,
        // but if the counter is non-zero then only KO animations are running.
        // This assumption is not checked.
        if (gBattleStruct->battlerKOAnimsRunning > 0)
            gBattleStruct->battlerKOAnimsRunning--;
        DestroyTask(taskId);
    }
}

void LaunchAnimationTaskForFrontSprite(struct Sprite *sprite, enum AnimFunctionIDs frontAnimId)
{
    u8 taskId = CreateTask(Task_HandleMonAnimation, 128);
    gTasks[taskId].tPtrHi = (u32)(sprite) >> 16;
    gTasks[taskId].tPtrLo = (u32)(sprite);
    gTasks[taskId].tAnimId = frontAnimId;
}
void StopMonFrontSpriteAnimationTask(void)
{
    u8 taskId = FindTaskIdByFunc(Task_HandleMonAnimation);
    if (taskId != TASK_NONE)
        DestroyTask(taskId);
}

void StartMonSummaryAnimation(struct Sprite *sprite, enum AnimFunctionIDs frontAnimId)
{
    // sDontFlip is expected to still be FALSE here, not explicitly cleared
    sIsSummaryAnim = TRUE;
    sprite->callback = sMonAnimFunctions[frontAnimId];
}

void LaunchAnimationTaskForBackSprite(struct Sprite *sprite, enum BackAnim backAnimSet)
{
    u8 nature, taskId, battler;
    enum AnimFunctionIDs animId;

    taskId = CreateTask(Task_HandleMonAnimation, 128);
    gTasks[taskId].tPtrHi = (u32)(sprite) >> 16;
    gTasks[taskId].tPtrLo = (u32)(sprite);

    battler = sprite->data[0];
    nature = GetNature(GetBattlerMon(battler));

    // * 3 below because each back anim has 3 variants depending on nature
    animId = 3 * backAnimSet + gNaturesInfo[nature].backAnim;
    gTasks[taskId].tAnimId = sBackAnimationIds[animId];
}

#undef tState
#undef tPtrHi
#undef tPtrLo
#undef tAnimId
#undef tBattlerId
#undef tSpeciesId

void SetSpriteCB_MonAnimDummy(struct Sprite *sprite)
{
    sprite->callback = MonAnimDummySpriteCallback;
}

static void SetAffineData(struct Sprite *sprite, s16 xScale, s16 yScale, u16 rotation)
{
    u8 matrixNum;
    struct ObjAffineSrcData affineSrcData;
    struct OamMatrix dest;

    affineSrcData.xScale = xScale;
    affineSrcData.yScale = yScale;
    affineSrcData.rotation = rotation;

    matrixNum = sprite->oam.matrixNum;

    ObjAffineSet(&affineSrcData, &dest, 1, 2);
    gOamMatrices[matrixNum].a = dest.a;
    gOamMatrices[matrixNum].b = dest.b;
    gOamMatrices[matrixNum].c = dest.c;
    gOamMatrices[matrixNum].d = dest.d;
}

static void HandleStartAffineAnim(struct Sprite *sprite)
{
    sprite->oam.affineMode = ST_OAM_AFFINE_DOUBLE;
    sprite->affineAnims = sMonAffineAnims;

    if (sIsSummaryAnim == TRUE)
        InitSpriteAffineAnim(sprite);

    if (!sprite->sDontFlip)
        StartSpriteAffineAnim(sprite, 1);
    else
        StartSpriteAffineAnim(sprite, 0);

    CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);
    sprite->affineAnimPaused = TRUE;
}

static void HandleSetAffineData(struct Sprite *sprite, s16 xScale, s16 yScale, u16 rotation)
{
    if (!sprite->sDontFlip)
    {
        xScale *= -1;
        rotation *= -1;
    }

    SetAffineData(sprite, xScale, yScale, rotation);
}

static void TryFlipX(struct Sprite *sprite)
{
    if (!sprite->sDontFlip)
        sprite->x2 *= -1;
}

static bool32 InitAnimData(u8 id)
{
    if (id >= MAX_BATTLERS_COUNT)
    {
        return FALSE;
    }
    else
    {
        sAnims[id].rotation = 0;
        sAnims[id].delay = 0;
        sAnims[id].runs = 1;
        sAnims[id].speed = 0;
        sAnims[id].data = 0;
        return TRUE;
    }
}

static u8 AddNewAnim(void)
{
    sAnimIdx = (sAnimIdx + 1) % MAX_BATTLERS_COUNT;
    InitAnimData(sAnimIdx);
    return sAnimIdx;
}

static void ResetSpriteAfterAnim(struct Sprite *sprite)
{
    sprite->oam.affineMode = ST_OAM_AFFINE_NORMAL;
    CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);

    if (sIsSummaryAnim == TRUE)
    {
        if (!sprite->sDontFlip)
            sprite->hFlip = TRUE;
        else
            sprite->hFlip = FALSE;

        FreeOamMatrix(sprite->oam.matrixNum);
        sprite->oam.matrixNum |= (sprite->hFlip << 3);
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
    }
#ifdef BUGFIX
    else
    {
        // FIX: Reset these back to normal after they were changed so Poké Ball catch/release
        // animations without a screen transition in between don't break
        sprite->affineAnims = gAffineAnims_BattleSpriteOpponentSide;
    }
#endif // BUGFIX
}

static void Anim_None(struct Sprite *sprite)
{
        sprite->callback = WaitAnimEnd;
}

static void WaitAnimEnd(struct Sprite *sprite)
{
    if (sprite->animEnded)
        sprite->callback = SpriteCallbackDummy;
}
