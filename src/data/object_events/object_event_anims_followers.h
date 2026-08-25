// Follower overworld animation commands and sAnimTable_Following (4x4 frame layout).

static const union AnimCmd sAnim_FaceSouth2F[] =
{
    ANIMCMD_FRAME(0, 16),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_FaceNorth2F[] =
{
    ANIMCMD_FRAME(2, 16),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_FaceWest2F[] =
{
    ANIMCMD_FRAME(4, 16),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_FaceEast2F[] =
{
    ANIMCMD_FRAME(4, 16, .hFlip = TRUE),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoSouth2F[] =
{
    ANIMCMD_FRAME(0, 6),
    ANIMCMD_FRAME(1, 6),
    ANIMCMD_FRAME(1, 6),
    ANIMCMD_FRAME(0, 6),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoNorth2F[] =
{
    ANIMCMD_FRAME(2, 6),
    ANIMCMD_FRAME(3, 6),
    ANIMCMD_FRAME(3, 6),
    ANIMCMD_FRAME(2, 6),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoWest2F[] =
{
    ANIMCMD_FRAME(4, 6),
    ANIMCMD_FRAME(5, 6),
    ANIMCMD_FRAME(5, 6),
    ANIMCMD_FRAME(4, 6),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoEast2F[] =
{
    ANIMCMD_FRAME(4, 6, .hFlip = TRUE),
    ANIMCMD_FRAME(5, 6, .hFlip = TRUE),
    ANIMCMD_FRAME(5, 6, .hFlip = TRUE),
    ANIMCMD_FRAME(4, 6, .hFlip = TRUE),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoFastSouth2F[] =
{
    ANIMCMD_FRAME(0, 4),
    ANIMCMD_FRAME(1, 4),
    ANIMCMD_FRAME(1, 4),
    ANIMCMD_FRAME(0, 4),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoFastNorth2F[] =
{
    ANIMCMD_FRAME(2, 4),
    ANIMCMD_FRAME(3, 4),
    ANIMCMD_FRAME(3, 4),
    ANIMCMD_FRAME(2, 4),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoFastWest2F[] =
{
    ANIMCMD_FRAME(4, 4),
    ANIMCMD_FRAME(5, 4),
    ANIMCMD_FRAME(5, 4),
    ANIMCMD_FRAME(4, 4),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_GoFastEast2F[] =
{
    ANIMCMD_FRAME(4, 4, .hFlip = TRUE),
    ANIMCMD_FRAME(5, 4, .hFlip = TRUE),
    ANIMCMD_FRAME(5, 4, .hFlip = TRUE),
    ANIMCMD_FRAME(4, 4, .hFlip = TRUE),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballSouth[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(0, 8),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballNorth[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(2, 8),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballWest[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(4, 8),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballEast[] =
{
    ANIMCMD_FRAME(0, 1, .hFlip = TRUE),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(4, 8, .hFlip = TRUE),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_EnterSouth[] =
{
    ANIMCMD_FRAME(0, 8),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_EnterNorth[] =
{
    ANIMCMD_FRAME(2, 8),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_EnterWest[] =
{
    ANIMCMD_FRAME(4, 8),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_EnterEast[] =
{
    ANIMCMD_FRAME(4, 8, .hFlip = TRUE),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(0, 3),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballFastSouth[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(0, 2),
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballFastNorth[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(2, 2),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballFastWest[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(4, 2),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd sAnim_ExitPokeballFastEast[] =
{
    ANIMCMD_FRAME(0, 1, .hFlip = TRUE),
    ANIMCMD_FRAME(1, 1),
    ANIMCMD_FRAME(2, 1),
    ANIMCMD_FRAME(3, 1),
    ANIMCMD_FRAME(4, 1),
    ANIMCMD_FRAME(4, 2, .hFlip = TRUE),
    ANIMCMD_FRAME(4, 1, .hFlip = TRUE),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd *const sAnimTable_Following[] = {
    [ANIM_STD_FACE_SOUTH] = sAnim_FaceSouth2F,
    [ANIM_STD_FACE_NORTH] = sAnim_FaceNorth2F,
    [ANIM_STD_FACE_WEST] = sAnim_FaceWest2F,
    [ANIM_STD_FACE_EAST] = sAnim_FaceEast2F,
    [ANIM_STD_GO_SOUTH] = sAnim_GoSouth2F,
    [ANIM_STD_GO_NORTH] = sAnim_GoNorth2F,
    [ANIM_STD_GO_WEST] = sAnim_GoWest2F,
    [ANIM_STD_GO_EAST] = sAnim_GoEast2F,
    [ANIM_STD_GO_FAST_SOUTH] = sAnim_GoFastSouth2F,
    [ANIM_STD_GO_FAST_NORTH] = sAnim_GoFastNorth2F,
    [ANIM_STD_GO_FAST_WEST] = sAnim_GoFastWest2F,
    [ANIM_STD_GO_FAST_EAST] = sAnim_GoFastEast2F,
    [ANIM_STD_GO_FASTER_SOUTH] = sAnim_EnterSouth,
    [ANIM_STD_GO_FASTER_NORTH] = sAnim_EnterNorth,
    [ANIM_STD_GO_FASTER_WEST] = sAnim_EnterWest,
    [ANIM_STD_GO_FASTER_EAST] = sAnim_EnterEast,
    [ANIM_STD_GO_FASTEST_SOUTH] = sAnim_ExitPokeballSouth,
    [ANIM_STD_GO_FASTEST_NORTH] = sAnim_ExitPokeballNorth,
    [ANIM_STD_GO_FASTEST_WEST] = sAnim_ExitPokeballWest,
    [ANIM_STD_GO_FASTEST_EAST] = sAnim_ExitPokeballEast,
    [ANIM_EXIT_POKEBALL_FAST_SOUTH] = sAnim_ExitPokeballFastSouth,
    [ANIM_EXIT_POKEBALL_FAST_NORTH] = sAnim_ExitPokeballFastNorth,
    [ANIM_EXIT_POKEBALL_FAST_WEST] = sAnim_ExitPokeballFastWest,
    [ANIM_EXIT_POKEBALL_FAST_EAST] = sAnim_ExitPokeballFastEast,
};
