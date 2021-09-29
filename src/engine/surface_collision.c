#include <PR/ultratypes.h>

#include "sm64.h"
#include "game/debug.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
// #include "game/rendering_graph_node.h"
#include "math_util.h"
#include "surface_collision.h"
#include "surface_load.h"
#include "game/puppyprint.h"

/**************************************************
 *                      WALLS                     *
 **************************************************/

#define CALC_OFFSET(vert, next_step) {          \
    if ((vert)[1] != 0.0f) {                    \
        v = (v2[1] / (vert)[1]);                \
        if ((v < 0.0f) || (v > 1.0f)) next_step;\
        d00 = (((vert)[0] * v) - v2[0]);        \
        d01 = (((vert)[2] * v) - v2[2]);        \
        invDenom = sqrtf(sqr(d00) + sqr(d01));  \
        offset   = (invDenom - margin_radius);  \
        if (offset > 0.0f) next_step;           \
        goto check_collision;                   \
    }                                           \
    next_step;                                  \
}

/**
 * Iterate through the list of walls until all walls are checked and
 * have given their wall push.
 */
static s32 find_wall_collisions_from_list(struct SurfaceNode *surfaceNode, struct WallCollisionData *data) {
    const f32 corner_threshold = -0.9f;
    register struct Surface *surf;
    register f32 offset;
    register f32 radius = data->radius;
    register Vec3f pos = { data->x, data->y + data->offsetY, data->z };
    register Vec3f v0, v1, v2;
    register f32 d00, d01, d11, d20, d21;
    register f32 invDenom;
    register f32 v, w;
    register f32 margin_radius = (radius - 1.0f);
    register TerrainData type = SURFACE_DEFAULT;
    s32 numCols = 0;
// #if EXTENDED_BOUNDS_MODE
//     const float down_scale = (1.0f / gWorldScale);
//     radius *= down_scale;
//     x *= down_scale;
//     y *= down_scale;
//     z *= down_scale;
//     margin_radius *= down_scale;
// #endif
    // Max collision radius = 200
    if (radius > 200.0f) {
        radius = 200.0f;
    }
    // Stay in this loop until out of walls.
    while (surfaceNode != NULL) {
        surf        = surfaceNode->surface;
        surfaceNode = surfaceNode->next;
        type        = surf->type;
        // Exclude a large number of walls immediately to optimize.
        if ((type == SURFACE_NEW_WATER) || (type == SURFACE_NEW_WATER_BOTTOM)) continue;
        // Determine if checking for the camera or not.
        if (gCheckingSurfaceCollisionsForCamera) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION) {
                continue;
            }
        } else {
            // Ignore camera only surfaces.
            if (type == SURFACE_CAMERA_BOUNDARY) {
                continue;
            }
            // If an object can pass through a vanish cap wall, pass through.
            if (type == SURFACE_VANISH_CAP_WALLS && gCurrentObject != NULL) {
                // If an object can pass through a vanish cap wall, pass through.
                if (gCurrentObject->activeFlags & ACTIVE_FLAG_MOVE_THROUGH_GRATE) {
                    continue;
                }
                // If Mario has a vanish cap, pass through the vanish cap wall.
                if (gCurrentObject == gMarioObject && (gMarioState->flags & MARIO_VANISH_CAP)) {
                    continue;
                }
            }
        }
        if (pos[1] < surf->lowerY || pos[1] > surf->upperY) {
            continue;
        }
        // Dot of normal and pos, + origin offset
        offset = (surf->normal.x * pos[0]) + (surf->normal.y * pos[1]) + (surf->normal.z * pos[2]) + surf->originOffset;
        if (offset < -radius || offset > radius) {
            continue;
        }
        vec3_diff(v0, surf->vertex2, surf->vertex1);
        vec3_diff(v1, surf->vertex3, surf->vertex1);
        vec3_diff(v2, pos,           surf->vertex1);
        // Face
        d00 = vec3_dot(v0, v0);
        d01 = vec3_dot(v0, v1);
        d11 = vec3_dot(v1, v1);
        d20 = vec3_dot(v2, v0);
        d21 = vec3_dot(v2, v1);
        invDenom = 1.0f / ((d00 * d11) - (d01 * d01));
        v = ((d11 * d20) - (d01 * d21)) * invDenom;
        if (v < 0.0f || v > 1.0f) {
            goto edge_1_2;
        }
        w = (d00 * d21 - d01 * d20) * invDenom;
        if (w < 0.0f || w > 1.0f || v + w > 1.0f) {
            goto edge_1_2;
        }
        pos[0] += surf->normal.x * (radius - offset);
        pos[2] += surf->normal.z * (radius - offset);
        goto hasCollision;
    edge_1_2:
        if (offset < 0) continue;
        CALC_OFFSET(v0, goto edge_1_3);
    edge_1_3:
        CALC_OFFSET(v1, goto edge_2_3);
    edge_2_3:
        vec3_diff(v1, surf->vertex3, surf->vertex2);
        vec3_diff(v2, pos, surf->vertex2);
        CALC_OFFSET(v1, continue);
    check_collision:
        invDenom = offset / invDenom;
        pos[0] += (d00 *= invDenom);
        pos[2] += (d01 *= invDenom);
        margin_radius += 0.01f;
        if ((d00 * surf->normal.x) + (d01 * surf->normal.z) < (corner_threshold * offset)) {
            continue;
        }
    hasCollision:
        // (Unreferenced Walls) Since this only returns the first MAX_REFEREMCED_WALLS walls,
        // this can lead to wall interaction being missed. Typically unreferenced walls
        // come from only using one wall, however.
        if (data->numWalls < MAX_REFEREMCED_WALLS) {
            data->walls[data->numWalls++] = surf;
        }
        numCols++;
    }
// #if EXTENDED_BOUNDS_MODE
//     x *= gWorldScale;
//     y *= gWorldScale;
//     z *= gWorldScale;
// #endif
    data->x = pos[0];
    data->z = pos[2];
    return numCols;
}

/**
 * Formats the position and wall search for find_wall_collisions.
 */
s32 f32_find_wall_collision(f32 *xPtr, f32 *yPtr, f32 *zPtr, f32 offsetY, f32 radius) {
    struct WallCollisionData collision;
    s32 numCollisions = 0;

    collision.offsetY = offsetY;
    collision.radius = radius;

    collision.x = *xPtr;
    collision.y = *yPtr;
    collision.z = *zPtr;

    collision.numWalls = 0;

    numCollisions = find_wall_collisions(&collision);

    *xPtr = collision.x;
    *yPtr = collision.y;
    *zPtr = collision.z;

    return numCollisions;
}

/**
 * Find wall collisions and receive their push.
 */
s32 find_wall_collisions(struct WallCollisionData *colData) {
    struct SurfaceNode *node;
    s32 cellX, cellZ;
    s32 numCollisions = 0;
    s32 x = colData->x;
    s32 z = colData->z;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    colData->numWalls = 0;

    if (is_outside_level_bounds(x, z)) {
        return numCollisions;
    }

    // World (level) consists of a 16x16 grid. Find where the collision is on
    // the grid (round toward -inf)
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    node = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next;
    numCollisions += find_wall_collisions_from_list(node, colData);

    // Check for surfaces that are a part of level geometry.
    node = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next;
    numCollisions += find_wall_collisions_from_list(node, colData);

    // Increment the debug tracker.
    gNumCalls.wall++;

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return numCollisions;
}

/**
 * Collides with walls and returns the most recent wall.
 */
void resolve_and_return_wall_collisions(Vec3f pos, f32 offset, f32 radius, struct WallCollisionData *collisionData) {
    collisionData->x = pos[0];
    collisionData->y = pos[1];
    collisionData->z = pos[2];
    collisionData->radius = radius;
    collisionData->offsetY = offset;

	find_wall_collisions(collisionData);

    pos[0] = collisionData->x;
    pos[1] = collisionData->y;
    pos[2] = collisionData->z;
}

/**************************************************
 *                     CEILINGS                   *
 **************************************************/

void add_ceil_margin(s32 *x, s32 *z, Vec3s target1, Vec3s target2, f32 margin) {
	register f32 diff_x, diff_z, invDenom;
	diff_x = target1[0] - *x + target2[0] - *x;
	diff_z = target1[2] - *z + target2[2] - *z;
	invDenom = margin / sqrtf(sqr(diff_x) + sqr(diff_z));
	*x += diff_x * invDenom;
	*z += diff_z * invDenom;
}

/**
 * Iterate through the list of ceilings and find the first ceiling over a given point.
 */
static struct Surface *find_ceil_from_list(struct SurfaceNode *surfaceNode, s32 x, s32 y, s32 z, f32 *pheight) {
    const f32 margin = 1.5f;
    register struct Surface *surf;
    Vec3i vx, vz;
    f32 height;
    struct Surface *ceil = NULL;
    *pheight = CELL_HEIGHT_LIMIT;
    // Stay in this loop until out of ceilings.
    while (surfaceNode != NULL) {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;
        if (y > surf->upperY) {
            continue;
        }
        vx[0] = surf->vertex1[0];
        vz[0] = surf->vertex1[2];
		if (surf->type != SURFACE_HANGABLE) {
			add_ceil_margin(&vx[0], &vz[0], surf->vertex2, surf->vertex3, margin);
        }
        vx[1] = surf->vertex2[0];
        vz[1] = surf->vertex2[2];
        if (surf->type != SURFACE_HANGABLE) {
			add_ceil_margin(&vx[1], &vz[1], surf->vertex3, surf->vertex1, margin);
        }
        // Checking if point is in bounds of the triangle laterally.
        if ((vz[0] - z) * (vx[1] - vx[0]) - (vx[0] - x) * (vz[1] - vz[0]) > 0) {
            continue;
        }
        // Slight optimization by checking these later.
        vx[2] = surf->vertex3[0];
        vz[2] = surf->vertex3[2];
		if (surf->type != SURFACE_HANGABLE) {
			add_ceil_margin(&vx[2], &vz[2], surf->vertex1, surf->vertex2, margin);
        }
        if ((vz[1] - z) * (vx[2] - vx[1]) - (vx[1] - x) * (vz[2] - vz[1]) > 0) {
            continue;
        }
        if ((vz[2] - z) * (vx[0] - vx[2]) - (vx[2] - x) * (vz[0] - vz[2]) > 0) {
            continue;
        }
        // Determine if checking for the camera or not.
        if (surf->type == SURFACE_NEW_WATER || surf->type == SURFACE_NEW_WATER_BOTTOM) {
            continue;
        }
        if (gCheckingSurfaceCollisionsForCamera) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION) {
                continue;
            }
        } else if (surf->type == SURFACE_CAMERA_BOUNDARY) {
            // Ignore camera only surfaces.
            continue;
        }
        // Find the ceil height at the specific point.
        height = get_surface_height_at_location(x, z, surf);
        if (height > *pheight) {
            continue;
        }
        // Checks for ceiling interaction
        if (y > height) {
            continue;
        }
        if (y >= surf->upperY) {
            continue;
        }
        *pheight = height;
        ceil = surf;
        if (height == y) {
            break;
        }
    }
    return ceil;
}

/**
 * Find the lowest ceiling above a given position and return the height.
 */
f32 find_ceil(f32 posX, f32 posY, f32 posZ, struct Surface **pceil) {
    s32 cellZ, cellX;
    struct Surface *ceil, *dynamicCeil;
    struct SurfaceNode *surfaceList;
    f32 height = CELL_HEIGHT_LIMIT;
    f32 dynamicHeight = CELL_HEIGHT_LIMIT;
    s32 x, y, z;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    x = posX;
    y = posY;
    z = posZ;
    *pceil = NULL;

    if (is_outside_level_bounds(x, z)) {
        return height;
    }

    // Each level is split into cells to limit load, find the appropriate cell.
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next;
    dynamicCeil = find_ceil_from_list(surfaceList, x, y, z, &dynamicHeight);

    // Check for surfaces that are a part of level geometry.
    surfaceList = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next;
    ceil = find_ceil_from_list(surfaceList, x, y, z, &height);

    if (dynamicHeight < height) {
        ceil = dynamicCeil;
        height = dynamicHeight;
    }

    *pceil = ceil;

    // Increment the debug tracker.
    gNumCalls.ceil++;

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return height;
}

/**************************************************
 *                     FLOORS                     *
 **************************************************/

/**
 * Find the height of the highest floor below an object.
 */
f32 unused_obj_find_floor_height(struct Object *obj) {
    struct Surface *floor;
    f32 floorHeight = find_floor(obj->oPosX, obj->oPosY, obj->oPosZ, &floor);
    return floorHeight;
}

/**
 * Iterate through the list of floors and find the first floor under a given point.
 */
static struct Surface *find_floor_from_list(struct SurfaceNode *surfaceNode, s32 x, s32 y, s32 z, f32 *pheight) {
    register struct Surface *surf;
    register Vec3i vx, vz;
    f32 height;
    struct Surface *floor = NULL;
    *pheight = FLOOR_LOWER_LIMIT;
    // Iterate through the list of floors until there are no more floors.
    while (surfaceNode != NULL) {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;
        if (y < surf->lowerY - 30) {
            continue;
        }
        vx[0] = surf->vertex1[0];
        vz[0] = surf->vertex1[2];
        vx[1] = surf->vertex2[0];
        vz[1] = surf->vertex2[2];
        // Check that the point is within the triangle bounds.
        if ((vz[0] - z) * (vx[1] - vx[0]) - (vx[0] - x) * (vz[1] - vz[0]) < 0) {
            continue;
        }
        // To slightly save on computation time, set this later.
        vx[2] = surf->vertex3[0];
        vz[2] = surf->vertex3[2];
        if ((vz[1] - z) * (vx[2] - vx[1]) - (vx[1] - x) * (vz[2] - vz[1]) < 0) {
            continue;
        }
        if ((vz[2] - z) * (vx[0] - vx[2]) - (vx[2] - x) * (vz[0] - vz[2]) < 0) {
            continue;
        }
        // Determine if we are checking for the camera or not.
        if (gCheckingSurfaceCollisionsForCamera != 0) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION || surf->type == SURFACE_NEW_WATER || surf->type == SURFACE_NEW_WATER_BOTTOM) {
                continue;
            }
        } else if (surf->type == SURFACE_CAMERA_BOUNDARY) {
            // If we are not checking for the camera, ignore camera only floors.
            continue;
        }
        // Find the height of the floor at a given location.
        height = get_surface_height_at_location(x, z, surf);
        if (height < *pheight) {
            continue;
        }
        // Checks for floor interaction with a 78 unit buffer.
        if (y < (height - 78.0f)) {
            continue;
        }
        *pheight = height;
        floor = surf;
        if (height - 78.0f == y) {
            break;
        }
    }
    return floor;
}


static s16 check_within_triangle_bounds(s32 x, s32 z, struct Surface *surf) {
    register Vec3i vx, vz;
    vx[0] = surf->vertex1[0];
    vz[0] = surf->vertex1[2];
    vx[1] = surf->vertex2[0];
    vz[1] = surf->vertex2[2];
    if ((vz[0] - z) * (vx[1] - vx[0]) - (vx[0] - x) * (vz[1] - vz[0]) < 0) return FALSE;
    vx[2] = surf->vertex3[0];
    vz[2] = surf->vertex3[2];
    if ((vz[1] - z) * (vx[2] - vx[1]) - (vx[1] - x) * (vz[2] - vz[1]) < 0) return FALSE;
    if ((vz[2] - z) * (vx[0] - vx[2]) - (vx[2] - x) * (vz[0] - vz[2]) < 0) return FALSE;
    return TRUE;
}

/**
 * Iterate through the list of water floors and find the first water floor under a given point.
 */
struct Surface *find_water_floor_from_list(struct SurfaceNode *surfaceNode, s32 x, s32 y, s32 z, f32 *pheight) {
    register struct Surface *surf;
    struct Surface *floor = NULL;
    struct SurfaceNode *topSurfaceNode = surfaceNode;
    struct SurfaceNode *bottomSurfaceNode = surfaceNode;
    f32 height = FLOOR_LOWER_LIMIT;
    f32 bottomHeight = FLOOR_LOWER_LIMIT;

    // Iterate through the list of water floors until there are no more water floors.
    while (bottomSurfaceNode != NULL) {
        f32 curBottomHeight = FLOOR_LOWER_LIMIT;
        surf = bottomSurfaceNode->surface;
        bottomSurfaceNode = bottomSurfaceNode->next;

        if (surf->type != SURFACE_NEW_WATER_BOTTOM || !check_within_triangle_bounds(x, z, surf)) continue;

        curBottomHeight = get_surface_height_at_location(x, z, surf);

        if (curBottomHeight < y - 78.0f) continue;
        if (curBottomHeight >= y - 78.0f) bottomHeight = curBottomHeight;
    }

    // Iterate through the list of water tops until there are no more water tops.
    while (topSurfaceNode != NULL) {
        f32 curHeight = FLOOR_LOWER_LIMIT;
        surf = topSurfaceNode->surface;
        topSurfaceNode = topSurfaceNode->next;

        if (surf->type == SURFACE_NEW_WATER_BOTTOM || !check_within_triangle_bounds(x, z, surf)) continue;

        curHeight = get_surface_height_at_location(x, z, surf);

        if (bottomHeight != FLOOR_LOWER_LIMIT && curHeight > bottomHeight) continue;

        if (curHeight > height) {
            height = curHeight;
            *pheight = curHeight;
            floor = surf;
        }
    }

    return floor;
}

/**
 * Find the height of the highest floor below a point.
 */
f32 find_floor_height(f32 x, f32 y, f32 z) {
    struct Surface *floor;

    f32 floorHeight = find_floor(x, y, z, &floor);

    return floorHeight;
}

/**
 * Find the highest dynamic floor under a given position. Perhaps originally static
 * and dynamic floors were checked separately.
 */
f32 unused_find_dynamic_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface **pfloor) {
    struct SurfaceNode *surfaceList;
    struct Surface *floor;
    f32 floorHeight = FLOOR_LOWER_LIMIT;

    // Would normally cause PUs, but dynamic floors unload at that range.
    s32 x = xPos;
    s32 y = yPos;
    s32 z = zPos;

    // Each level is split into cells to limit load, find the appropriate cell.
    s32 cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    s32 cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    floor = find_floor_from_list(surfaceList, x, y, z, &floorHeight);

    *pfloor = floor;

    return floorHeight;
}

/**
 * Find the highest floor under a given position and return the height.
 */
f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface **pfloor) {
    s32 cellZ, cellX;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    struct Surface *floor, *dynamicFloor;
    struct SurfaceNode *surfaceList;

    f32 height       = FLOOR_LOWER_LIMIT;
    f32 dynamicHeight = FLOOR_LOWER_LIMIT;

    //! (Parallel Universes) Because position is casted to an s16, reaching higher
    // float locations  can return floors despite them not existing there.
    //(Dynamic floors will unload due to the range.)
    s32 x = xPos;
    s32 y = yPos;
    s32 z = zPos;

    *pfloor = NULL;

    if (is_outside_level_bounds(x, z)) {
        #if PUPPYPRINT_DEBUG
        collisionTime[perfIteration] += osGetTime() - first;
        #endif
        return height;
    }

    // Each level is split into cells to limit load, find the appropriate cell.
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    dynamicFloor = find_floor_from_list(surfaceList, x, y, z, &dynamicHeight);

    // Check for surfaces that are a part of level geometry.
    surfaceList = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    floor = find_floor_from_list(surfaceList, x, y, z, &height);

    // To prevent the Merry-Go-Round room from loading when Mario passes above the hole that leads
    // there, SURFACE_INTANGIBLE is used. This prevent the wrong room from loading, but can also allow
    // Mario to pass through.
    if (!gFindFloorIncludeSurfaceIntangible) {
        //! (BBH Crash) Most NULL checking is done by checking the height of the floor returned
        //  instead of checking directly for a NULL floor. If this check returns a NULL floor
        //  (happens when there is no floor under the SURFACE_INTANGIBLE floor) but returns the height
        //  of the SURFACE_INTANGIBLE floor instead of the typical -11000 returned for a NULL floor.
        if (floor != NULL && floor->type == SURFACE_INTANGIBLE) {
            floor = find_floor_from_list(surfaceList, x, (s32)(height - 200.0f), z, &height);
        }
    } else {
        // To prevent accidentally leaving the floor tangible, stop checking for it.
        gFindFloorIncludeSurfaceIntangible = FALSE;
    }

    // If a floor was missed, increment the debug counter.
    if (floor == NULL) {
        gNumFindFloorMisses++;
    }

    if (dynamicHeight > height) {
        floor = dynamicFloor;
        height = dynamicHeight;
    }

    *pfloor = floor;

    // Increment the debug tracker.
    gNumCalls.floor++;

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return height;
}

/**
 * Find the highest water floor under a given position and return the height.
 */
f32 find_water_floor(s32 xPos, s32 yPos, s32 zPos, struct Surface **pfloor) {
    s32 cellZ, cellX;

    struct Surface *floor = NULL;
    struct SurfaceNode *surfaceList;

    f32 height = FLOOR_LOWER_LIMIT;

    s32 x = xPos;
    s32 y = yPos;
    s32 z = zPos;

    if (is_outside_level_bounds(x, z)) {
        return height;
    }

    // Each level is split into cells to limit load, find the appropriate cell.
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces that are a part of level geometry.
    surfaceList = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WATER].next;
    floor = find_water_floor_from_list(surfaceList, x, y, z, &height);

    if (floor == NULL) {
        height = FLOOR_LOWER_LIMIT;
    } else {
        *pfloor = floor;
    }

    return height;
}

/**************************************************
 *               ENVIRONMENTAL BOXES              *
 **************************************************/

/**
 * Finds the height of water at a given location.
 */
s32 find_water_level_and_floor(s32 x, s32 z, struct Surface **pfloor) {
    s32 i;
    s32 numRegions;
    s32 val;
    s32 loX, hiX, loZ, hiZ;
    s32 waterLevel = FLOOR_LOWER_LIMIT;
    TerrainData *p = gEnvironmentRegions;
    struct Surface *floor = NULL;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    if (gCheckingSurfaceCollisionsForCamera) {
        waterLevel = find_water_floor(x, gLakituState.pos[1], z, &floor);
    } else {
        waterLevel = find_water_floor(x, gMarioState->pos[1], z, &floor);
    }

    if (p != NULL && waterLevel == FLOOR_LOWER_LIMIT) {
        numRegions = *p++;

        for (i = 0; i < numRegions; i++) {
            val = *p++;
            loX = *p++;
            loZ = *p++;
            hiX = *p++;
            hiZ = *p++;

            // If the location is within a water box and it is a water box.
            // Water is less than 50 val only, while above is gas and such.
            if (loX < x && x < hiX && loZ < z && z < hiZ && val < 50) {
                // Set the water height. Since this breaks, only return the first height.
                waterLevel = *p;
                break;
            }
            p++;
        }
    } else {
        *pfloor = floor;
    }

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return waterLevel;
}

/**
 * Finds the height of water at a given location.
 */
s32 find_water_level(s32 x, s32 z) {
    s32 i;
    s32 numRegions;
    s32 val;
    s32 loX, hiX, loZ, hiZ;
    s32 waterLevel = FLOOR_LOWER_LIMIT;
    TerrainData *p = gEnvironmentRegions;
    struct Surface *floor;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    if (gCheckingSurfaceCollisionsForCamera) {
        waterLevel = find_water_floor(x, gLakituState.pos[1], z, &floor);
    } else {
        waterLevel = find_water_floor(x, gMarioState->pos[1], z, &floor);
    }

    if (p != NULL && waterLevel == FLOOR_LOWER_LIMIT) {
        numRegions = *p++;

        for (i = 0; i < numRegions; i++) {
            val = *p++;
            loX = *p++;
            loZ = *p++;
            hiX = *p++;
            hiZ = *p++;

            // If the location is within a water box and it is a water box.
            // Water is less than 50 val only, while above is gas and such.
            if (loX < x && x < hiX && loZ < z && z < hiZ && val < 50) {
                // Set the water height. Since this breaks, only return the first height.
                waterLevel = *p;
                break;
            }
            p++;
        }
    }

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return waterLevel;
}

/**
 * Finds the height of the poison gas (used only in HMC) at a given location.
 */
s32 find_poison_gas_level(s32 x, s32 z) {
    s32 i;
    s32 numRegions;
    s32 val;
    s32 loX, hiX, loZ, hiZ;
    s32 gasLevel = FLOOR_LOWER_LIMIT;
    TerrainData *p = gEnvironmentRegions;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    if (p != NULL) {
        numRegions = *p++;

        for (i = 0; i < numRegions; i++) {
            val = *p;

            if (val >= 50) {
                loX = p[1];
                loZ = p[2];
                hiX = p[3];
                hiZ = p[4];

                // If the location is within a gas's box and it is a gas box.
                // Gas has a value of 50, 60, etc.
                if (loX < x && x < hiX && loZ < z && z < hiZ && val % 10 == 0) {
                    // Set the gas height. Since this breaks, only return the first height.
                    gasLevel = p[5];
                    break;
                }
            }

            p += 6;
        }
    }

    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif

    return gasLevel;
}

/**************************************************
 *                      DEBUG                     *
 **************************************************/

/**
 * Finds the length of a surface list for debug purposes.
 */
static s32 surface_list_length(struct SurfaceNode *list) {
    s32 count = 0;

    while (list != NULL) {
        list = list->next;
        count++;
    }

    return count;
}

/**
 * Print the area,number of walls, how many times they were called,
 * and some allocation information.
 */
void debug_surface_list_info(f32 xPos, f32 zPos) {
    struct SurfaceNode *list;
    s32 numFloors = 0;
    s32 numWalls = 0;
    s32 numCeils = 0;

    s32 cellX = (xPos + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    s32 cellZ = (zPos + LEVEL_BOUNDARY_MAX) / CELL_SIZE;

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_FLOORS].next;
    numFloors += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_FLOORS].next;
    numFloors += surface_list_length(list);

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_WALLS].next;
    numWalls += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_WALLS].next;
    numWalls += surface_list_length(list);

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_CEILS].next;
    numCeils += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_CEILS].next;
    numCeils += surface_list_length(list);

    print_debug_top_down_mapinfo("area   %x", cellZ * NUM_CELLS + cellX);

    // Names represent ground, walls, and roofs as found in SMS.
    print_debug_top_down_mapinfo("dg %d", numFloors);
    print_debug_top_down_mapinfo("dw %d", numWalls);
    print_debug_top_down_mapinfo("dr %d", numCeils);

    set_text_array_x_y(80, -3);

    print_debug_top_down_mapinfo("%d", gNumCalls.floor);
    print_debug_top_down_mapinfo("%d", gNumCalls.wall);
    print_debug_top_down_mapinfo("%d", gNumCalls.ceil);

    set_text_array_x_y(-80, 0);

    // listal- List Allocated?, statbg- Static Background?, movebg- Moving Background?
    print_debug_top_down_mapinfo("listal %d", gSurfaceNodesAllocated);
    print_debug_top_down_mapinfo("statbg %d", gNumStaticSurfaces);
    print_debug_top_down_mapinfo("movebg %d", gSurfacesAllocated - gNumStaticSurfaces);

    gNumCalls.floor = 0;
    gNumCalls.ceil = 0;
    gNumCalls.wall = 0;
}

/**
 * An unused function that finds and interacts with any type of surface.
 * Perhaps an original implementation of surfaces before they were more specialized.
 */
s32 unused_resolve_floor_or_ceil_collisions(s32 checkCeil, f32 *px, f32 *py, f32 *pz, f32 radius,
                                            struct Surface **psurface, f32 *surfaceHeight) {
    f32 nx, ny, nz, oo;
    f32 x = *px;
    f32 y = *py;
    f32 z = *pz;
    f32 offset, distance;

    *psurface = NULL;

    if (checkCeil) {
        *surfaceHeight = find_ceil(x, y, z, psurface);
    } else {
        *surfaceHeight = find_floor(x, y, z, psurface);
    }

    if (*psurface == NULL) {
        return -1;
    }

    nx = (*psurface)->normal.x;
    ny = (*psurface)->normal.y;
    nz = (*psurface)->normal.z;
    oo = (*psurface)->originOffset;

    offset = (nx * x) + (ny * y) + (nz * z) + oo;
    distance = offset >= 0 ? offset : -offset;

    // Interesting surface interaction that should be surf type independent.
    if (distance < radius) {
        *px += nx * (radius - offset);
        *py += ny * (radius - offset);
        *pz += nz * (radius - offset);

        return 1;
    }

    return 0;
}

/**************************************************
 *                    RAYCASTING                  *
 **************************************************/

#define RAY_OFFSET 30.0f /*How many units to extrapolate surfaces when testing for a raycast*/
#define RAY_STEPS 4 /*How many steps to do when casting rays, default to quartersteps.*/

s32 ray_surface_intersect(Vec3f orig, Vec3f dir, f32 dir_length, struct Surface *surface, Vec3f hit_pos, f32 *length) {
    Vec3f v0, v1, v2, e1, e2, h, s, q;
    f32 a, f, u, v;
    Vec3f add_dir;
    Vec3f norm;

    //Ignore certain surface types.
    if (surface->type == SURFACE_INTANGIBLE || surface->flags & SURFACE_FLAG_NO_CAM_COLLISION)
        return FALSE;

    // Get surface normal and some other stuff
    vec3_set(norm, 0, surface->normal.y, 0);
    vec3_mul_val(norm, RAY_OFFSET);

    vec3_copy(v0, surface->vertex1);
    vec3_copy(v1, surface->vertex2);
    vec3_copy(v2, surface->vertex3);

    vec3_add(v0, norm);
    vec3_add(v1, norm);
    vec3_add(v2, norm);

    vec3_diff(e1, v1, v0);
    vec3_diff(e2, v2, v0);

    vec3_cross(h, dir, e2);

    // Check if we're perpendicular from the surface
    a = vec3_dot(e1, h);
    if (a > -0.00001f && a < 0.00001f) {
        return FALSE;
    }
    // Check if we're making contact with the surface
    f = 1.0f / a;

    vec3_diff(s, orig, v0);
    u = f * vec3_dot(s, h);
    if (u < 0.0f || u > 1.0f) {
        return FALSE;
    }
    vec3_cross(q, s, e1);
    v = f * vec3_dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) {
        return FALSE;
    }
    // Get the length between our origin and the surface contact point
    *length = f * vec3_dot(e2, q);
    if (*length <= 0.00001 || *length > dir_length) {
        return FALSE;
    }
    // Successful contact
    vec3f_copy(add_dir, dir);
    vec3_mul_val(add_dir, *length);
    vec3_sum(hit_pos, orig, add_dir);
    return TRUE;
}

void find_surface_on_ray_list(struct SurfaceNode *list, Vec3f orig, Vec3f dir, f32 dir_length, struct Surface **hit_surface, Vec3f hit_pos, f32 *max_length) {
    s32 hit;
    f32 length;
    Vec3f chk_hit_pos;
    f32 top, bottom;
    #if PUPPYPRINT_DEBUG
    OSTime first = osGetTime();
    #endif

    // Get upper and lower bounds of ray
    if (dir[1] >= 0.0f) {
        top = orig[1] + dir[1] * dir_length;
        bottom = orig[1];
    } else {
        top = orig[1];
        bottom = orig[1] + dir[1] * dir_length;
    }

    // Iterate through every surface of the list
    for (; list != NULL; list = list->next) {
        // Reject surface if out of vertical bounds
        if (list->surface->lowerY > top || list->surface->upperY < bottom) {
            continue;
        }
        // Check intersection between the ray and this surface
        if ((hit = ray_surface_intersect(orig, dir, dir_length, list->surface, chk_hit_pos, &length)) != 0) {
            if (length <= *max_length) {
                *hit_surface = list->surface;
                vec3f_copy(hit_pos, chk_hit_pos);
                *max_length = length;
            }
        }
    }
    #if PUPPYPRINT_DEBUG
    collisionTime[perfIteration] += osGetTime()-first;
    #endif
}

void find_surface_on_ray_cell(s32 cellX, s32 cellZ, Vec3f orig, Vec3f normalized_dir, f32 dir_length, struct Surface **hit_surface, Vec3f hit_pos, f32 *max_length, s32 flags) {
    // Skip if OOB
    if (cellX >= 0 && cellX <= (NUM_CELLS - 1) && cellZ >= 0 && cellZ <= (NUM_CELLS - 1)) {
        // Iterate through each surface in this partition
        if (normalized_dir[1] > -0.99999f && flags & RAYCAST_FIND_CEIL) {
            find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
            find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
        }
        if (normalized_dir[1] < 0.99999f && flags & RAYCAST_FIND_FLOOR) {
            find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
            find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
        }
        if (flags & RAYCAST_FIND_WALL) {
            find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
            find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
        }
        if (flags & RAYCAST_FIND_WATER) {
            find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WATER].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
            find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WATER].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
        }
    }
}

void find_surface_on_ray(Vec3f orig, Vec3f dir, struct Surface **hit_surface, Vec3f hit_pos, s32 flags) {
    f32 max_length;
    s32 cellZ, cellX, cellPrevX, cellPrevZ;
    f32 fCellZ, fCellX;
    f32 dir_length;
    Vec3f normalized_dir;
    f32 step, dx, dz;
    s32 i;

    // Set that no surface has been hit
    *hit_surface = NULL;
    vec3_sum(hit_pos, orig, dir);

    // Get normalized direction
    dir_length = vec3_mag(dir);
    max_length = dir_length;
    vec3f_copy(normalized_dir, dir);
    vec3f_normalize(normalized_dir);

    // Get our cell coordinate
    fCellX = (orig[0] + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    fCellZ = (orig[2] + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    cellX = fCellX;
    cellZ = fCellZ;
    cellPrevX = cellX;
    cellPrevZ = cellZ;

    // Don't do DDA if straight down
    if (normalized_dir[1] >= 0.99999f || normalized_dir[1] <= -0.99999f) {
        find_surface_on_ray_cell(cellX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length, flags);
        return;
    }

    // Get cells we cross using DDA
    if (ABS(dir[0]) >= ABS(dir[2]))
        step = RAY_STEPS * ABS(dir[0]) / CELL_SIZE;
    else
        step = RAY_STEPS * ABS(dir[2]) / CELL_SIZE;

    dx = dir[0] / step / CELL_SIZE;
    dz = dir[2] / step / CELL_SIZE;

    for (i = 0; i < step && *hit_surface == NULL; i++) {
        find_surface_on_ray_cell(cellX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length, flags);

        // Move cell coordinate
        fCellX += dx;
        fCellZ += dz;
        cellPrevX = cellX;
        cellPrevZ = cellZ;
        cellX = fCellX;
        cellZ = fCellZ;

        if ((cellPrevX != cellX) && (cellPrevZ != cellZ)) {
            find_surface_on_ray_cell(cellX, cellPrevZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length, flags);
            find_surface_on_ray_cell(cellPrevX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length, flags);
        }
    }
}

