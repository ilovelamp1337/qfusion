#include "qcommon.h"
#include "cm_local.h"

/*
* CM_PointLeafnum
*/
int CM_PointLeafnum( const cmodel_state_t *cms, const vec3_t p, int topNodeHint ) {
	assert( topNodeHint >= 0 );

	int num = topNodeHint;
	cnode_t *node;

	if( !cms->numplanes ) {
		return 0; // sound may call this without map loaded

	}
	do {
		node = cms->map_nodes + num;
		num = node->children[PlaneDiff( p, node->plane ) < 0];
	} while( num >= 0 );

	return -1 - num;
}

/*
* CM_BoxLeafnums
*
* Fills in a list of all the leafs touched
*/
static void CM_BoxLeafnums_r( const cmodel_state_t *cms, int nodenum ) {
	int s;
	cnode_t *node;

	while( nodenum >= 0 ) {
		node = &cms->map_nodes[nodenum];
		s = BOX_ON_PLANE_SIDE( cms->leaf_mins, cms->leaf_maxs, node->plane ) - 1;

		if( s < 2 ) {
			nodenum = node->children[s];
			continue;
		}

		// go down both sides
		if( cms->leaf_topnode == -1 ) {
			cms->leaf_topnode = nodenum;
		}
		CM_BoxLeafnums_r( cms, node->children[0] );
		nodenum = node->children[1];
	}

	if( cms->leaf_count < cms->leaf_maxcount ) {
		cms->leaf_list[cms->leaf_count++] = -1 - nodenum;
	}
}

/*
* CM_BoxLeafnums
*/
int CM_BoxLeafnums( const cmodel_state_t *cms,
					const vec3_t mins, const vec3_t maxs,
					int *list, int listsize,
					int *topnode, int topNodeHint ) {
	assert( topNodeHint >= 0 );

	cms->leaf_list = list;
	cms->leaf_count = 0;
	cms->leaf_maxcount = listsize;
	cms->leaf_mins = mins;
	cms->leaf_maxs = maxs;

	cms->leaf_topnode = -1;

	CM_BoxLeafnums_r( cms, topNodeHint );

	// Make sure the hinted top node is a parent of found split node
	assert( !topNodeHint || cms->leaf_topnode > topNodeHint );

	if( topnode ) {
		*topnode = cms->leaf_topnode;
	}

	return cms->leaf_count;
}

/*
* CM_BrushContents
*/
static inline int CM_BrushContents( const cbrush_t *brush, const vec3_t p ) {
	int i;
	cbrushside_t *brushside;

	for( i = 0, brushside = brush->brushsides; i < brush->numsides; i++, brushside++ )
		if( PlaneDiff( p, &brushside->plane ) > 0 ) {
			return 0;
		}

	return brush->contents;
}

/*
* CM_PatchContents
*/
static inline int CM_PatchContents( const cface_t *patch, const vec3_t p ) {
	int i, c;
	const cbrush_t *facet;

	for( i = 0, facet = patch->facets; i < patch->numfacets; i++, facet++ )
		if( ( c = CM_BrushContents( facet, p ) ) ) {
			return c;
		}

	return 0;
}

/*
* CM_PointContents
*/
static int CM_PointContents( const cmodel_state_t *cms, const vec3_t p, const cmodel_t *cmodel, int topNodeHint ) {
	assert( topNodeHint >= 0 );

	int i, superContents, contents;
	int nummarkfaces, nummarkbrushes;
	cface_t *patch, *markface;
	cbrush_t *brush, *markbrush;

	if( !cms->numnodes ) {  // map not loaded
		return 0;
	}

	if( cmodel == cms->map_cmodels ) {
		cleaf_t *leaf;

		leaf = &cms->map_leafs[CM_PointLeafnum( cms, p, topNodeHint )];
		superContents = leaf->contents;

		markbrush = leaf->brushes;
		nummarkbrushes = leaf->numbrushes;

		markface = leaf->faces;
		nummarkfaces = leaf->numfaces;
	} else {
		superContents = ~0;

		markbrush = cmodel->brushes;
		nummarkbrushes = cmodel->numbrushes;

		markface = cmodel->faces;
		nummarkfaces = cmodel->numfaces;
	}

	contents = superContents;

	for( i = 0; i < nummarkbrushes; i++ ) {
		brush = &markbrush[i];

		// check if brush adds something to contents
		if( contents & brush->contents ) {
			if( !( contents &= ~CM_BrushContents( brush, p ) ) ) {
				return superContents;
			}
		}
	}

	for( i = 0; i < nummarkfaces; i++ ) {
		patch = &markface[i];

		// check if patch adds something to contents
		if( contents & patch->contents ) {
			if( BoundsIntersect( p, p, patch->mins, patch->maxs ) ) {
				if( !( contents &= ~CM_PatchContents( patch, p ) ) ) {
					return superContents;
				}
			}
		}
	}

	return ~contents & superContents;
}

/*
* CM_TransformedPointContents
*
* Handles offseting and rotation of the end points for moving and
* rotating entities
*/
int CM_TransformedPointContents( const cmodel_state_t *cms, const vec3_t p, const cmodel_t *cmodel,
								 const vec3_t origin, const vec3_t angles, int topNodeHint ) {
	vec3_t p_l;

	if( !cms->numnodes ) { // map not loaded
		return 0;
	}

	if( !cmodel || cmodel == cms->map_cmodels ) {
		cmodel = cms->map_cmodels;
		origin = vec3_origin;
		angles = vec3_origin;
	} else {
		if( !origin ) {
			origin = vec3_origin;
		}
		if( !angles ) {
			angles = vec3_origin;
		}
	}

	// subtract origin offset
	VectorSubtract( p, origin, p_l );

	// rotate start and end into the models frame of reference
	if( ( angles[0] || angles[1] || angles[2] ) && !cmodel->builtin ) {
		vec3_t temp;
		mat3_t axis;

		AnglesToAxis( angles, axis );
		VectorCopy( p_l, temp );
		Matrix3_TransformVector( axis, temp, p_l );
	}

	return CM_PointContents( cms, p_l, cmodel, topNodeHint );
}

int CM_FindTopNodeForBox( const cmodel_state_t *cms, const vec3_t mins, const vec3_t maxs, unsigned maxValue ) {
	// Spread bounds a bit to ensure inclusion of boundary planes in an enclosing node
	vec3_t testedMins { -2, -2, -2 };
	vec3_t testedMaxs { +2, +2, +2 };
	VectorAdd( mins, testedMins, testedMins );
	VectorAdd( maxs, testedMaxs, testedMaxs );

	int lastGoodNode = 0;
	for(;; ) {
		const cnode_t *node = &cms->map_nodes[lastGoodNode];
		int side = BOX_ON_PLANE_SIDE( testedMins, testedMaxs, node->plane );
		// Stop at finding a splitting node
		if( side == 3 ) {
			return lastGoodNode;
		}
		int child = node->children[side - 1];
		// Stop at leaves
		if( child < 0 ) {
			return lastGoodNode;
		}
		// Stop at maximum allowed numeric value of a child
		if( (unsigned)child > maxValue ) {
			return lastGoodNode;
		}
		lastGoodNode = child;
	}
}