#define COMP_OW_TILE_TAG_BASE 0xCE00
#define OBJ_EVENT_PAL_TAG_WHITE (OBJ_EVENT_PAL_TAG_NONE - 1)
#define IS_POW_OF_TWO(n) (((n) & ((n) - 1)) == 0)

#define sFollowerDuration data[3]
#define sFollowerSpeedFlip data[6]

static s16 ReallocSpriteTiles(struct Sprite *sprite, u32 byteSize)
{
    s16 i;
    bool32 wasVisible = sprite->invisible;
    sprite->invisible = TRUE;

    i = CopySprite(sprite, sprite->x, sprite->y, 0xFF);
    if (i < MAX_SPRITES)
    {
        DestroySprite(&gSprites[i]);
        i = AllocSpriteTiles(byteSize / TILE_SIZE_4BPP);
        if (i >= 0)
        {
            CpuFastFill16(0, (u8 *)OBJ_VRAM0 + TILE_SIZE_4BPP * i, byteSize);
            sprite->oam.tileNum = i;
        }
    }
    else
    {
        i = -1;
    }

    sprite->invisible = wasVisible;
    return i;
}

#if OW_GFX_COMPRESS
u16 LoadSheetGraphicsInfo(const struct ObjectEventGraphicsInfo *info, u16 uuid, struct Sprite *sprite)
{
    u16 tag = info->tileTag;
    if (tag != TAG_NONE || info->compressed)
    {
        u32 sheetSpan = GetSpanPerImage(info->oam->shape, info->oam->size);
        u16 oldTiles = 0;
        u16 tileStart;
        bool32 oldInvisible;
        if (tag == TAG_NONE)
            tag = COMP_OW_TILE_TAG_BASE + uuid;

        if (sprite)
        {
            oldInvisible = sprite->invisible;
            oldTiles = sprite->sheetTileStart;
            sprite->sheetTileStart = 0;
        }

        tileStart = GetSpriteTileStartByTag(tag);
        if (tileStart == TAG_NONE)
        {
            struct SpriteFrameImage image = {.size = info->size, .data = info->images->data};
            struct SpriteTemplate template = {.tileTag = tag, .images = &image};
            tileStart = LoadCompressedSpriteSheetByTemplate(&template, TILE_SIZE_4BPP << sheetSpan);
            if (oldTiles)
            {
                FieldEffectFreeTilesIfUnused(oldTiles);
                if (tileStart <= 0)
                {
                    if (sprite)
                        sprite->invisible = TRUE;
                    tileStart = LoadCompressedSpriteSheetByTemplate(&template, TILE_SIZE_4BPP << sheetSpan);
                }
            }
        }
        else if (oldTiles && oldTiles != tileStart)
        {
            FieldEffectFreeTilesIfUnused(oldTiles);
        }

        if (sprite)
        {
            sprite->sheetTileStart = tileStart;
            sprite->sheetSpan = sheetSpan;
            sprite->usingSheet = TRUE;
            sprite->invisible = oldInvisible;
        }
    }
    else if (sprite && sprite->usingSheet)
    {
        sprite->oam.tileNum = sprite->sheetTileStart;
        sprite->usingSheet = FALSE;
    }
    else if (sprite && !sprite->sheetTileStart && sprite->oam.size != info->oam->size)
    {
        ReallocSpriteTiles(sprite, info->images->size);
    }
    return tag;
}
#endif

static u8 UpdateSpritePalette(const struct SpritePalette *spritePalette, struct Sprite *sprite)
{
    sprite->inUse = FALSE;
    FieldEffectFreePaletteIfUnused(sprite->oam.paletteNum);
    sprite->inUse = TRUE;
    return sprite->oam.paletteNum = LoadSpritePalette(spritePalette);
}

static void ObjectEventSetGraphics(struct ObjectEvent *objectEvent, const struct ObjectEventGraphicsInfo *graphicsInfo)
{
    struct Sprite *sprite = &gSprites[objectEvent->spriteId];
    if (graphicsInfo->paletteTag != OBJ_EVENT_PAL_TAG_DYNAMIC
        && graphicsInfo->paletteTag != OBJ_EVENT_PAL_TAG_NONE)
    {
        u32 i = FindObjectEventPaletteIndexByTag(graphicsInfo->paletteTag);
        if (i != 0xFF)
            UpdateSpritePalette(&sObjectEventSpritePalettes[i], sprite);
    }

    if (LARGE_OW_SUPPORT && !OW_GFX_COMPRESS && graphicsInfo->oam->size != sprite->oam.size)
        ReallocSpriteTiles(sprite, graphicsInfo->images->size);

#if OW_GFX_COMPRESS
    LoadSheetGraphicsInfo(graphicsInfo, objectEvent->graphicsId, sprite);
#endif

    sprite->oam.shape = graphicsInfo->oam->shape;
    sprite->oam.size = graphicsInfo->oam->size;
    sprite->images = graphicsInfo->images;
    sprite->anims = graphicsInfo->anims;
    sprite->subspriteTables = graphicsInfo->subspriteTables;
    objectEvent->inanimate = graphicsInfo->inanimate;
    SetSpritePosToMapCoords(objectEvent->currentCoords.x, objectEvent->currentCoords.y, &sprite->x, &sprite->y);
    sprite->centerToCornerVecX = -(graphicsInfo->width >> 1);
    sprite->centerToCornerVecY = -(graphicsInfo->height >> 1);
    sprite->x += 8;
    sprite->y += 16 + sprite->centerToCornerVecY;
    if (objectEvent->trackedByCamera)
        CameraObjectReset1();
}

struct Pokemon *GetFirstLiveMon(void)
{
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        if ((OW_MON_ALLOWED_SPECIES && GetMonData(mon, MON_DATA_SPECIES_OR_EGG) != VarGet(OW_MON_ALLOWED_SPECIES))
            || (OW_MON_ALLOWED_MET_LVL && GetMonData(mon, MON_DATA_MET_LEVEL) != VarGet(OW_MON_ALLOWED_MET_LVL))
            || (OW_MON_ALLOWED_MET_LOC && GetMonData(mon, MON_DATA_MET_LOCATION) != VarGet(OW_MON_ALLOWED_MET_LOC)))
            continue;

        if (GetMonData(mon, MON_DATA_HP) > 0
            && !GetMonData(mon, MON_DATA_IS_EGG)
            && !GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG))
            return mon;
    }
    return NULL;
}

struct ObjectEvent *GetFollowerObject(void)
{
    u32 i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (gObjectEvents[i].localId == OBJ_EVENT_ID_FOLLOWER && gObjectEvents[i].active)
            return &gObjectEvents[i];
    }
    return NULL;
}

static const struct ObjectEventGraphicsInfo *SpeciesToGraphicsInfo(u16 species, u8 form)
{
    const struct ObjectEventGraphicsInfo *graphicsInfo;
    if (species >= NUM_SPECIES)
        return &gPokemonObjectGraphics[SPECIES_NONE];
    graphicsInfo = &gPokemonObjectGraphics[species];
    if (graphicsInfo->paletteTag == OBJ_EVENT_PAL_TAG_NONE && graphicsInfo->tileTag == 0xFFFF)
        return &gPokemonObjectGraphics[SPECIES_NONE];
    return graphicsInfo;
}

static u8 LoadDynamicFollowerPalette(u16 species, u8 form, bool32 shiny)
{
    u32 paletteNum;
    struct SpritePalette spritePalette = {.tag = shiny ? (species + SPECIES_SHINY_TAG) : species};

    if ((paletteNum = IndexOfSpritePaletteTag(spritePalette.tag)) != 0xFF)
        return paletteNum;

    spritePalette.data = (u16 *)((shiny ? gMonShinyPaletteTable : gMonPaletteTable)[species].data);
    if (IsLZ77Data(spritePalette.data, PLTT_SIZE_4BPP, PLTT_SIZE_4BPP))
    {
        LZ77UnCompWram((const u32 *)spritePalette.data, gDecompressionBuffer);
        spritePalette.data = (void *)gDecompressionBuffer;
    }

    paletteNum = LoadSpritePalette(&spritePalette);
    if (paletteNum == 0xFF)
        return 0;
    UpdateSpritePaletteWithWeather(paletteNum);
    return paletteNum;
}

static void FollowerSetGraphics(struct ObjectEvent *objEvent, u16 species, u8 form, bool8 shiny)
{
    const struct ObjectEventGraphicsInfo *graphicsInfo = SpeciesToGraphicsInfo(species, form);
    ObjectEventSetGraphics(objEvent, graphicsInfo);
    objEvent->graphicsId = (OBJ_EVENT_GFX_MON_BASE + species) & OBJ_EVENT_GFX_SPECIES_MASK;
    objEvent->graphicsId |= form << OBJ_EVENT_GFX_SPECIES_BITS;
    objEvent->shiny = shiny;
    if (graphicsInfo->paletteTag == OBJ_EVENT_PAL_TAG_DYNAMIC)
    {
        struct Sprite *sprite = &gSprites[objEvent->spriteId];
        sprite->inUse = FALSE;
        FieldEffectFreePaletteIfUnused(sprite->oam.paletteNum);
        sprite->inUse = TRUE;
        sprite->oam.paletteNum = LoadDynamicFollowerPalette(species, form, shiny);
    }
    else
    {
        UpdateSpritePaletteWithWeather(gSprites[objEvent->spriteId].oam.paletteNum);
    }
}

static bool8 GetMonInfo(struct Pokemon *mon, u16 *species, u8 *form, u8 *shiny)
{
    if (!mon)
    {
        *species = SPECIES_NONE;
        *form = 0;
        *shiny = 0;
        return FALSE;
    }
    *species = GetMonData(mon, MON_DATA_SPECIES);
    *shiny = IsMonShiny(mon);
    *form = 0;
    return TRUE;
}

static bool8 GetFollowerInfo(u16 *species, u8 *form, u8 *shiny)
{
    return GetMonInfo(GetFirstLiveMon(), species, form, shiny);
}

static bool32 IsFollowerAllowedOnMap(void)
{
#if OW_MON_OUTDOORS_ONLY
    return gMapHeader.mapType == MAP_TYPE_TOWN
        || gMapHeader.mapType == MAP_TYPE_ROUTE;
#else
    if (gMapHeader.mapType == MAP_TYPE_INDOOR)
        return FALSE;
    return TRUE;
#endif
}

void UpdateFollowingPokemon(void)
{
    struct ObjectEvent *objEvent = GetFollowerObject();
    struct Sprite *sprite;
    u16 species;
    bool8 shiny;
    u8 form;
    bool8 shouldHaveFollower;

    shouldHaveFollower = GetFollowerInfo(&species, &form, &shiny)
        && IsFollowerAllowedOnMap()
        && !FlagGet(FLAG_TEMP_HIDE_FOLLOWER);

    if (!shouldHaveFollower)
    {
        RemoveFollowingPokemon();
        return;
    }

    if (objEvent == NULL)
    {
        u32 objId = gPlayerAvatar.objectEventId;
        struct ObjectEventTemplate template;

        template.localId = OBJ_EVENT_ID_FOLLOWER;
        template.graphicsId = ((OBJ_EVENT_GFX_MON_BASE + species) & OBJ_EVENT_GFX_SPECIES_MASK) | (form << OBJ_EVENT_GFX_SPECIES_BITS);
        template.kind = OBJ_KIND_NORMAL;
        template.x = gSaveBlock1Ptr->pos.x;
        template.y = gSaveBlock1Ptr->pos.y;
        template.objUnion.normal.elevation = gObjectEvents[objId].active ? gObjectEvents[objId].currentElevation : 3;
        template.objUnion.normal.movementType = MOVEMENT_TYPE_FOLLOW_PLAYER;
        template.objUnion.normal.movementRangeX = 0;
        template.objUnion.normal.movementRangeY = 0;
        template.objUnion.normal.trainerType = TRAINER_TYPE_NONE;
        template.objUnion.normal.trainerRange_berryTreeId = (form & 0x1F) | (shiny << 5);
        template.script = NULL;
        template.flagId = 0;
        if ((objId = SpawnSpecialObjectEvent(&template)) >= OBJECT_EVENTS_COUNT)
            return;
        objEvent = &gObjectEvents[objId];
        FollowerSetGraphics(objEvent, species, form, shiny);
        {
            s16 behindX = gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x;
            s16 behindY = gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y;
            MoveCoords(GetOppositeDirection(GetPlayerFacingDirection()), &behindX, &behindY);
            MoveObjectEventToMapCoords(objEvent, behindX, behindY);
        }
        objEvent->invisible = TRUE;
        gSprites[objEvent->spriteId].y2 = 0;
    }
    sprite = &gSprites[objEvent->spriteId];
    if (species != OW_SPECIES(objEvent) || shiny != objEvent->shiny || form != OW_FORM(objEvent))
    {
        MoveObjectEventToMapCoords(objEvent, gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x, gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y);
        FollowerSetGraphics(objEvent, species, form, shiny);
        objEvent->invisible = TRUE;
    }
    sprite->data[6] = 0;
}

void RemoveFollowingPokemon(void)
{
    struct ObjectEvent *objectEvent = GetFollowerObject();
    if (objectEvent == NULL)
        return;
    RemoveObjectEvent(objectEvent);
}

void RestoreFollowerObjectEventGraphics(struct ObjectEvent *objectEvent)
{
    u16 species;
    u8 form, shiny;

    if (objectEvent == NULL || objectEvent->localId != OBJ_EVENT_ID_FOLLOWER)
        return;

    if (!GetFollowerInfo(&species, &form, &shiny))
        return;

    objectEvent->graphicsId = ((OBJ_EVENT_GFX_MON_BASE + species) & OBJ_EVENT_GFX_SPECIES_MASK) | (form << OBJ_EVENT_GFX_SPECIES_BITS);
    objectEvent->shiny = shiny;
}

static bool32 IsFollowerVisible(void)
{
    return IsFollowerAllowedOnMap()
        && !(TestPlayerAvatarFlags(FOLLOWER_INVISIBLE_FLAGS)
        || MetatileBehavior_IsSurfable(gObjectEvents[gPlayerAvatar.objectEventId].previousMetatileBehavior)
        || MetatileBehavior_IsForcedMovementTile(gObjectEvents[gPlayerAvatar.objectEventId].currentMetatileBehavior));
}

static bool8 TryStartFollowerAppear(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    s16 targetX, targetY;

    if (!objectEvent->invisible || !IsFollowerVisible())
        return FALSE;
    if (ArePlayerFieldControlsLocked() && !FlagGet(FLAG_SAFE_FOLLOWER_MOVEMENT))
        return FALSE;
    if (PlayerGetCopyableMovement() == COPY_MOVE_JUMP2)
        return FALSE;

    targetX = gObjectEvents[gPlayerAvatar.objectEventId].previousCoords.x;
    targetY = gObjectEvents[gPlayerAvatar.objectEventId].previousCoords.y;
    MoveObjectEventToMapCoords(objectEvent, targetX, targetY);
    ClearObjectEventMovement(objectEvent, sprite);
    ObjectEventSetSingleMovement(objectEvent, sprite, MOVEMENT_ACTION_EXIT_POKEBALL);
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    if (OW_MON_BOBBING == TRUE)
        sprite->y2 = 0;
    return TRUE;
}

static bool32 UpdateMonMoveInPlace(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    if (!objectEvent->singleMovementActive)
    {
        ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkInPlaceNormalMovementAction(objectEvent->facingDirection));
        objectEvent->singleMovementActive = TRUE;
        return TRUE;
    }
    else if (ObjectEventExecSingleMovementAction(objectEvent, sprite))
    {
        objectEvent->singleMovementActive = FALSE;
    }
    else if (OW_MON_BOBBING == TRUE && (sprite->data[3] & 7) == 2)
    {
        sprite->y2 ^= -1;
    }
    return FALSE;
}

bool8 MovementType_FollowPlayer_Shadow(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    ClearObjectEventMovement(objectEvent, sprite);
    if (!IsFollowerVisible())
    {
        objectEvent->invisible = TRUE;
        MoveObjectEventToMapCoords(objectEvent,
            gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x,
            gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y);
        objectEvent->triggerGroundEffectsOnMove = FALSE;
        return FALSE;
    }
    if (objectEvent->invisible)
    {
        MoveObjectEventToMapCoords(objectEvent,
            gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x,
            gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y);
        objectEvent->triggerGroundEffectsOnMove = FALSE;
    }
    sprite->data[1] = 1;
    return TRUE;
}

bool8 MovementType_FollowPlayer_Active(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    if (!IsFollowerVisible())
    {
        if (objectEvent->invisible)
        {
            sprite->data[1] = 0;
            return FALSE;
        }
        ClearObjectEventMovement(objectEvent, sprite);
        ObjectEventSetSingleMovement(objectEvent, sprite, MOVEMENT_ACTION_ENTER_POKEBALL);
        objectEvent->singleMovementActive = TRUE;
        sprite->data[1] = 2;
        return TRUE;
    }
    if (TryStartFollowerAppear(objectEvent, sprite))
        return TRUE;
    return gFollowPlayerMovementFuncs[PlayerGetCopyableMovement()](objectEvent, sprite, GetPlayerMovementDirection(), NULL);
}

bool8 MovementType_FollowPlayer_Moving(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    if (ObjectEventExecSingleMovementAction(objectEvent, sprite))
    {
        objectEvent->singleMovementActive = FALSE;
        objectEvent->facingDirectionLocked = FALSE;
        if (sprite->data[1])
            sprite->data[1] = 1;
    }
    else if (objectEvent->movementActionId < MOVEMENT_ACTION_EXIT_POKEBALL)
    {
        if (OW_MON_BOBBING == TRUE
            && objectEvent->movementActionId >= MOVEMENT_ACTION_WALK_NORMAL_DOWN
            && objectEvent->movementActionId <= MOVEMENT_ACTION_WALK_NORMAL_RIGHT
            && (sprite->data[5] & 7) == 2)
            sprite->y2 ^= -1;
    }
    return FALSE;
}

bool8 FollowablePlayerMovement_Idle(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    sprite->y2 = 0;
    if (OW_MON_WANDER_WALK == TRUE)
    {
        if (UpdateMonMoveInPlace(objectEvent, sprite))
        {
            sprite->data[1] = 1;
            return TRUE;
        }
        return FALSE;
    }
    SetObjectEventDirection(objectEvent, GetPlayerFacingDirection());
    StartSpriteAnimIfDifferent(sprite, GetFaceDirectionAnimNum(objectEvent->facingDirection));
    return FALSE;
}

bool8 FollowablePlayerMovement_Step(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction;
    s16 x, y, targetX, targetY;
    u32 playerAction = gObjectEvents[gPlayerAvatar.objectEventId].movementActionId;

    targetX = gObjectEvents[gPlayerAvatar.objectEventId].previousCoords.x;
    targetY = gObjectEvents[gPlayerAvatar.objectEventId].previousCoords.y;
    x = gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.x;
    y = gObjectEvents[gPlayerAvatar.objectEventId].currentCoords.y;

    if ((x == targetX && y == targetY) || !IsFollowerVisible())
        return FALSE;

    x = objectEvent->currentCoords.x;
    y = objectEvent->currentCoords.y;
    ClearObjectEventMovement(objectEvent, sprite);

    if (objectEvent->invisible)
    {
        if (TryStartFollowerAppear(objectEvent, sprite))
            return TRUE;
        return FALSE;
    }
    else if (x == targetX && y == targetY)
    {
        return FALSE;
    }

    direction = GetDirectionToFace(x, y, targetX, targetY);
    if (ArePlayerFieldControlsLocked()
        && gObjectEvents[gPlayerAvatar.objectEventId].facingDirection != gObjectEvents[gPlayerAvatar.objectEventId].movementDirection)
    {
        direction = gObjectEvents[gPlayerAvatar.objectEventId].movementDirection;
        objectEvent->facingDirectionLocked = TRUE;
    }

    MoveCoords(direction, &x, &y);
    if (GetLedgeJumpDirection(x, y, direction) != DIR_NONE)
        ObjectEventSetSingleMovement(objectEvent, sprite, GetJump2MovementAction(direction));
    else if (playerAction >= MOVEMENT_ACTION_WALK_SLOW_DOWN && playerAction <= MOVEMENT_ACTION_WALK_SLOW_RIGHT)
        ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkSlowMovementAction(direction));
    else if (PlayerGetCopyableMovement() == COPY_MOVE_JUMP2)
        ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkSlowMovementAction(direction));
    else if (gSprites[gPlayerAvatar.spriteId].data[4] == MOVE_SPEED_FAST_1)
        ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkFastMovementAction(direction));
    else
        ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkNormalMovementAction(direction));
    sprite->y2 = 0;
    sprite->data[2] = 0;
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

bool8 FollowablePlayerMovement_GoSpeed1(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction = GetCopyDirection(gInitialMovementTypeFacingDirections[objectEvent->movementType], objectEvent->directionSequenceIndex, playerDirection);
    s16 x, y;
    ObjectEventMoveDestCoords(objectEvent, direction, &x, &y);
    ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkFastMovementAction(direction));
    if (GetCollisionAtCoords(objectEvent, x, y, direction) || (tileCallback != NULL && !tileCallback(MapGridGetMetatileBehaviorAt(x, y))))
        ObjectEventSetSingleMovement(objectEvent, sprite, GetFaceDirectionMovementAction(direction));
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

bool8 FollowablePlayerMovement_GoSpeed2(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction = GetCopyDirection(gInitialMovementTypeFacingDirections[objectEvent->movementType], objectEvent->directionSequenceIndex, playerDirection);
    s16 x, y;
    ObjectEventMoveDestCoords(objectEvent, direction, &x, &y);
    ObjectEventSetSingleMovement(objectEvent, sprite, GetWalkFasterMovementAction(direction));
    if (GetCollisionAtCoords(objectEvent, x, y, direction) || (tileCallback != NULL && !tileCallback(MapGridGetMetatileBehaviorAt(x, y))))
        ObjectEventSetSingleMovement(objectEvent, sprite, GetFaceDirectionMovementAction(direction));
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

bool8 FollowablePlayerMovement_Slide(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction = GetCopyDirection(gInitialMovementTypeFacingDirections[objectEvent->movementType], objectEvent->directionSequenceIndex, playerDirection);
    s16 x, y;
    ObjectEventMoveDestCoords(objectEvent, direction, &x, &y);
    ObjectEventSetSingleMovement(objectEvent, sprite, GetSlideMovementAction(direction));
    if (GetCollisionAtCoords(objectEvent, x, y, direction) || (tileCallback != NULL && !tileCallback(MapGridGetMetatileBehaviorAt(x, y))))
        ObjectEventSetSingleMovement(objectEvent, sprite, GetFaceDirectionMovementAction(direction));
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

bool8 FollowablePlayerMovement_JumpInPlace(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction = GetCopyDirection(gInitialMovementTypeFacingDirections[objectEvent->movementType], objectEvent->directionSequenceIndex, playerDirection);
    ObjectEventSetSingleMovement(objectEvent, sprite, GetJumpInPlaceMovementAction(direction));
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

bool8 FollowablePlayerMovement_GoSpeed4(struct ObjectEvent *objectEvent, struct Sprite *sprite, u8 playerDirection, bool8 tileCallback(u8))
{
    u32 direction = GetCopyDirection(gInitialMovementTypeFacingDirections[objectEvent->movementType], objectEvent->directionSequenceIndex, playerDirection);
    s16 x, y;
    ObjectEventMoveDestCoords(objectEvent, direction, &x, &y);
    ObjectEventSetSingleMovement(objectEvent, sprite, GetJumpMovementAction(direction));
    if (GetCollisionAtCoords(objectEvent, x, y, direction) || (tileCallback != NULL && !tileCallback(MapGridGetMetatileBehaviorAt(x, y))))
        ObjectEventSetSingleMovement(objectEvent, sprite, GetFaceDirectionMovementAction(direction));
    objectEvent->singleMovementActive = TRUE;
    sprite->data[1] = 2;
    return TRUE;
}

static u8 LoadFillColorPalette(u16 color, u16 paletteTag, struct Sprite *sprite)
{
    u16 paletteData[16];
    struct SpritePalette dynamicPalette = {.tag = paletteTag, .data = paletteData};
    CpuFill16(color, paletteData, PLTT_SIZE_4BPP);
    return UpdateSpritePalette(&dynamicPalette, sprite);
}

static void ObjectEventSetPokeballGfx(struct ObjectEvent *objEvent)
{
    ObjectEventSetGraphicsId(objEvent, OBJ_EVENT_GFX_ITEM_BALL);
}

static const union AffineAnimCmd sAffineAnim_PokeballExit[] =
{
    AFFINEANIMCMD_FRAME(0x40, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0x80, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xC0, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0x100, 0x100, 0, 0),
    AFFINEANIMCMD_END,
};

static const union AffineAnimCmd sAffineAnim_PokeballExitEast[] =
{
    AFFINEANIMCMD_FRAME(0xFFC0, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFF80, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFF40, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFF00, 0x100, 0, 0),
    AFFINEANIMCMD_END,
};

static const union AffineAnimCmd sAffineAnim_PokeballEnter[] =
{
    AFFINEANIMCMD_FRAME(0x100, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xC0, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0x80, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0x40, 0x100, 0, 0),
    AFFINEANIMCMD_END,
};

static const union AffineAnimCmd sAffineAnim_PokeballEnterEast[] =
{
    AFFINEANIMCMD_FRAME(0xFF00, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFF40, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFF80, 0x100, 0, 0),
    AFFINEANIMCMD_FRAME(0xFFC0, 0x100, 0, 0),
    AFFINEANIMCMD_END,
};

static const union AffineAnimCmd *const sAffineAnims_PokeballFollower[] =
{
    sAffineAnim_PokeballExit,
    sAffineAnim_PokeballExitEast,
    sAffineAnim_PokeballEnter,
    sAffineAnim_PokeballEnterEast,
};

bool8 MovementAction_ExitPokeball_Step0(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    u32 direction = gObjectEvents[gPlayerAvatar.objectEventId].facingDirection;
    u16 graphicsId = objectEvent->graphicsId;
    objectEvent->invisible = FALSE;
    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_DASH))
    {
        StartSpriteAnimInDirection(objectEvent, sprite, direction, GetJumpSpecialDirectionAnimNum(direction));
        sprite->sFollowerDuration = 8;
        sprite->sFollowerSpeedFlip = 0;
    }
    else
    {
        StartSpriteAnimInDirection(objectEvent, sprite, direction, GetMoveDirectionFastestAnimNum(direction));
        sprite->sFollowerDuration = 16;
        sprite->sFollowerSpeedFlip = 1;
    }
    if (direction == DIR_EAST && sprite->anims[ANIM_STD_FACE_EAST]->frame.hFlip)
        sprite->sFollowerSpeedFlip |= 1 << 4;
    ObjectEventSetPokeballGfx(objectEvent);
    objectEvent->graphicsId = graphicsId;
    objectEvent->inanimate = FALSE;
    return MovementAction_ExitPokeball_Step1(objectEvent, sprite);
}

bool8 MovementAction_ExitPokeball_Step1(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    u32 animStepFrame = (sprite->sFollowerSpeedFlip & 1) ? 7 : 3;
    if (--sprite->sFollowerDuration == 0)
    {
        sprite->data[2] = 2;
        sprite->animCmdIndex = 0;
        sprite->animPaused = TRUE;
        return TRUE;
    }
    else if (sprite->sFollowerDuration == animStepFrame)
    {
        FollowerSetGraphics(objectEvent, OW_SPECIES(objectEvent), OW_FORM(objectEvent), objectEvent->shiny);
        LoadFillColorPalette(RGB_WHITE, OBJ_EVENT_PAL_TAG_WHITE, sprite);
        sprite->affineAnims = sAffineAnims_PokeballFollower;
        if (LARGE_OW_SUPPORT && !IS_POW_OF_TWO(-sprite->centerToCornerVecX))
            return FALSE;
        sprite->oam.affineMode = ST_OAM_AFFINE_NORMAL;
        InitSpriteAffineAnim(sprite);
        StartSpriteAffineAnim(sprite, sprite->sFollowerSpeedFlip >> 4);
    }
    else if (sprite->sFollowerDuration == (animStepFrame >> 1))
    {
        sprite->affineAnimEnded = TRUE;
        FreeSpriteOamMatrix(sprite);
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
        FollowerSetGraphics(objectEvent, OW_SPECIES(objectEvent), OW_FORM(objectEvent), objectEvent->shiny);
    }
    return FALSE;
}

bool8 MovementAction_EnterPokeball_Step0(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    u32 direction = objectEvent->facingDirection;
    StartSpriteAnimInDirection(objectEvent, sprite, direction, GetMoveDirectionFasterAnimNum(direction));
    sprite->sFollowerDuration = 16;
    if (direction == DIR_EAST && sprite->anims[ANIM_STD_FACE_EAST]->frame.hFlip)
        sprite->sFollowerSpeedFlip = 3;
    else
        sprite->sFollowerSpeedFlip = 2;
    return MovementAction_EnterPokeball_Step1(objectEvent, sprite);
}

bool8 MovementAction_EnterPokeball_Step1(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    u16 graphicsId = objectEvent->graphicsId;
    if (--sprite->sFollowerDuration == 0)
    {
        sprite->data[2] = 2;
        return FALSE;
    }
    else if (sprite->sFollowerDuration == 11)
    {
        LoadFillColorPalette(RGB_WHITE, OBJ_EVENT_PAL_TAG_WHITE, sprite);
        sprite->subspriteTableNum = 0;
        if (LARGE_OW_SUPPORT && !IS_POW_OF_TWO(-sprite->centerToCornerVecX))
            return FALSE;
        sprite->affineAnims = sAffineAnims_PokeballFollower;
        sprite->oam.affineMode = ST_OAM_AFFINE_NORMAL;
        InitSpriteAffineAnim(sprite);
        StartSpriteAffineAnim(sprite, sprite->sFollowerSpeedFlip);
    }
    else if (sprite->sFollowerDuration == 7)
    {
        sprite->affineAnimEnded = TRUE;
        FreeSpriteOamMatrix(sprite);
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
        ObjectEventSetPokeballGfx(objectEvent);
        objectEvent->graphicsId = graphicsId;
        objectEvent->inanimate = FALSE;
    }
    return FALSE;
}

bool8 MovementAction_EnterPokeball_Step2(struct ObjectEvent *objectEvent, struct Sprite *sprite)
{
    FollowerSetGraphics(objectEvent, OW_SPECIES(objectEvent), OW_FORM(objectEvent), objectEvent->shiny);
    objectEvent->invisible = TRUE;
    sprite->data[1] = 0;
    sprite->sFollowerSpeedFlip = 0;
    sprite->animPaused = TRUE;
    return TRUE;
}

movement_type_def(MovementType_FollowPlayer, gMovementTypeFuncs_FollowPlayer)

#undef sFollowerDuration
#undef sFollowerSpeedFlip
