/* cr.c - content rule routines */
/* $OpenLDAP$ */
/*
 * Copyright 1998-2002 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

#include "portable.h"

#include <stdio.h>

#include <ac/ctype.h>
#include <ac/string.h>
#include <ac/socket.h>

#include "slap.h"
#include "ldap_pvt.h"

#ifdef SLAP_EXTENDED_SCHEMA

struct cindexrec {
	struct berval	cir_name;
	ContentRule	*cir_cr;
};

static Avlnode	*cr_index = NULL;
static ContentRule *cr_list = NULL;

static int
cr_index_cmp(
    struct cindexrec	*cir1,
    struct cindexrec	*cir2 )
{
	int i = cir1->cir_name.bv_len - cir2->cir_name.bv_len;
	if (i)
		return i;
	return strcasecmp( cir1->cir_name.bv_val, cir2->cir_name.bv_val );
}

static int
cr_index_name_cmp(
    struct berval	*name,
    struct cindexrec	*cir )
{
	int i = name->bv_len - cir->cir_name.bv_len;
	if (i)
		return i;
	return strncasecmp( name->bv_val, cir->cir_name.bv_val, name->bv_len );
}

ContentRule *
cr_find( const char *crname )
{
	struct berval bv;

	bv.bv_val = (char *)crname;
	bv.bv_len = strlen( crname );

	return( cr_bvfind( &bv ) );
}

ContentRule *
cr_bvfind( struct berval *crname )
{
	struct cindexrec	*cir;

	cir = (struct cindexrec *) avl_find( cr_index, crname,
            (AVL_CMP) cr_index_name_cmp );

	if ( cir != NULL ) {
		return( cir->cir_cr );
	}

	return( NULL );
}

void
cr_destroy( void )
{
	ContentRule *c, *n;

	avl_free(cr_index, ldap_memfree);
	for (c=cr_list; c; c=n)
	{
		n = c->scr_next;
		if (c->scr_auxiliaries) ldap_memfree(c->scr_auxiliaries);
		if (c->scr_required) ldap_memfree(c->scr_required);
		if (c->scr_allowed) ldap_memfree(c->scr_allowed);
		if (c->scr_precluded) ldap_memfree(c->scr_precluded);
		ldap_contentrule_free((LDAPContentRule *)c);
	}
}

static int
cr_insert(
    ContentRule		*scr,
    const char		**err
)
{
	ContentRule	**crp;
	struct cindexrec	*cir;
	char			**names;

	crp = &cr_list;
	while ( *crp != NULL ) {
		crp = &(*crp)->scr_next;
	}
	*crp = scr;

	if ( scr->scr_oid ) {
		cir = (struct cindexrec *)
			ch_calloc( 1, sizeof(struct cindexrec) );
		cir->cir_name.bv_val = scr->scr_oid;
		cir->cir_name.bv_len = strlen( scr->scr_oid );
		cir->cir_cr = scr;

		assert( cir->cir_name.bv_val );
		assert( cir->cir_cr );

		if ( avl_insert( &cr_index, (caddr_t) cir,
				 (AVL_CMP) cr_index_cmp,
				 (AVL_DUP) avl_dup_error ) )
		{
			*err = scr->scr_oid;
			ldap_memfree(cir);
			return SLAP_SCHERR_CR_DUP;
		}

		/* FIX: temporal consistency check */
		assert( cr_bvfind(&cir->cir_name) != NULL );
	}

	if ( (names = scr->scr_names) ) {
		while ( *names ) {
			cir = (struct cindexrec *)
				ch_calloc( 1, sizeof(struct cindexrec) );
			cir->cir_name.bv_val = *names;
			cir->cir_name.bv_len = strlen( *names );
			cir->cir_cr = scr;

			assert( cir->cir_name.bv_val );
			assert( cir->cir_cr );

			if ( avl_insert( &cr_index, (caddr_t) cir,
					 (AVL_CMP) cr_index_cmp,
					 (AVL_DUP) avl_dup_error ) )
			{
				*err = *names;
				ldap_memfree(cir);
				return SLAP_SCHERR_CR_DUP;
			}

			/* FIX: temporal consistency check */
			assert( cr_bvfind(&cir->cir_name) != NULL );

			names++;
		}
	}

	return 0;
}

static int
cr_add_auxiliaries(
    ContentRule		*scr,
	int			*op,
    const char		**err )
{
	int naux;

	if( scr->scr_oc_oids_aux == NULL ) return 0;
	
	for( naux=0; scr->scr_oc_oids_aux[naux]; naux++ ) {
		/* count them */ ;
	}

	scr->scr_auxiliaries = ch_calloc( naux+1, sizeof(ObjectClass *));

	for( naux=0; scr->scr_oc_oids_aux[naux]; naux++ ) {
		ObjectClass *soc = scr->scr_auxiliaries[naux]
			= oc_find(scr->scr_oc_oids_aux[naux]);
		if ( !soc ) {
			*err = scr->scr_oc_oids_aux[naux];
			return SLAP_SCHERR_CLASS_NOT_FOUND;
		}

		if( soc->soc_flags & SLAP_OC_OPERATIONAL ) (*op)++;

		if( soc->soc_kind != LDAP_SCHEMA_AUXILIARY ) {
			*err = scr->scr_oc_oids_aux[naux];
			return SLAP_SCHERR_CR_BAD_AUX;
		}
	}

	scr->scr_auxiliaries[naux] = NULL;

	return 0;
}

static int
cr_create_required(
    ContentRule		*scr,
	int			*op,
    const char		**err )
{
    char		**attrs = scr->scr_at_oids_must;
	char		**attrs1;
	AttributeType	*sat;

	if ( attrs ) {
		attrs1 = attrs;
		while ( *attrs1 ) {
			sat = at_find(*attrs1);
			if ( !sat ) {
				*err = *attrs1;
				return SLAP_SCHERR_ATTR_NOT_FOUND;
			}

			if( is_at_operational( sat )) (*op)++;

			if ( at_find_in_list(sat, scr->scr_required) < 0) {
				if ( at_append_to_list(sat, &scr->scr_required) ) {
					*err = *attrs1;
					return SLAP_SCHERR_OUTOFMEM;
				}
			} else {
				*err = *attrs1;
				return SLAP_SCHERR_CR_BAD_AT;
			}
			attrs1++;
		}
	}
	return 0;
}

static int
cr_create_allowed(
    ContentRule		*scr,
	int			*op,
    const char		**err )
{
    char		**attrs = scr->scr_at_oids_may;
	char		**attrs1;
	AttributeType	*sat;

	if ( attrs ) {
		attrs1 = attrs;
		while ( *attrs1 ) {
			sat = at_find(*attrs1);
			if ( !sat ) {
				*err = *attrs1;
				return SLAP_SCHERR_ATTR_NOT_FOUND;
			}

			if( is_at_operational( sat )) (*op)++;

			if ( at_find_in_list(sat, scr->scr_required) < 0 &&
				at_find_in_list(sat, scr->scr_allowed) < 0 )
			{
				if ( at_append_to_list(sat, &scr->scr_allowed) ) {
					*err = *attrs1;
					return SLAP_SCHERR_OUTOFMEM;
				}
			} else {
				*err = *attrs1;
				return SLAP_SCHERR_CR_BAD_AT;
			}
			attrs1++;
		}
	}
	return 0;
}

static int
cr_create_precluded(
    ContentRule		*scr,
	int			*op,
    const char		**err )
{
    char		**attrs = scr->scr_at_oids_not;
	char		**attrs1;
	AttributeType	*sat;

	if ( attrs ) {
		attrs1 = attrs;
		while ( *attrs1 ) {
			sat = at_find(*attrs1);
			if ( !sat ) {
				*err = *attrs1;
				return SLAP_SCHERR_ATTR_NOT_FOUND;
			}

			if( is_at_operational( sat )) (*op)++;

			/* FIXME: should also make sure attribute type is not
				a required attribute of the structural class or
				any auxiliary class */
			if ( at_find_in_list(sat, scr->scr_required) < 0 &&
				at_find_in_list(sat, scr->scr_allowed) < 0 &&
				at_find_in_list(sat, scr->scr_precluded) < 0 )
			{
				if ( at_append_to_list(sat, &scr->scr_precluded) ) {
					*err = *attrs1;
					return SLAP_SCHERR_OUTOFMEM;
				}
			} else {
				*err = *attrs1;
				return SLAP_SCHERR_CR_BAD_AT;
			}
			attrs1++;
		}
	}
	return 0;
}

int
cr_add(
    LDAPContentRule	*cr,
	int user,
    const char		**err
)
{
	ContentRule	*scr;
	int		code;
	int		op = 0;

	if ( cr->cr_names != NULL ) {
		int i;

		for( i=0; cr->cr_names[i]; i++ ) {
			if( !slap_valid_descr( cr->cr_names[i] ) ) {
				return SLAP_SCHERR_BAD_DESCR;
			}
		}
	}

	if ( !OID_LEADCHAR( cr->cr_oid[0] )) {
		/* Expand OID macros */
		char *oid = oidm_find( cr->cr_oid );
		if ( !oid ) {
			*err = cr->cr_oid;
			return SLAP_SCHERR_OIDM;
		}
		if ( oid != cr->cr_oid ) {
			ldap_memfree( cr->cr_oid );
			cr->cr_oid = oid;
		}
	}

	scr = (ContentRule *) ch_calloc( 1, sizeof(ContentRule) );
	AC_MEMCPY( &scr->scr_crule, cr, sizeof(LDAPContentRule) );

	scr->scr_sclass = oc_find(cr->cr_oid);
	if ( !scr->scr_sclass ) {
		*err = cr->cr_oid;
		return SLAP_SCHERR_CLASS_NOT_FOUND;
	}

	/* check object class usage */
	if( scr->scr_sclass->soc_kind != LDAP_SCHEMA_STRUCTURAL )
	{
		*err = cr->cr_oid;
		return SLAP_SCHERR_CR_BAD_STRUCT;
	}

	if( scr->scr_sclass->soc_flags & SLAP_OC_OPERATIONAL ) op++;

	code = cr_add_auxiliaries( scr, &op, err );
	if ( code != 0 ) return code;

	code = cr_create_required( scr, &op, err );
	if ( code != 0 ) return code;

	code = cr_create_allowed( scr, &op, err );
	if ( code != 0 ) return code;

	code = cr_create_precluded( scr, &op, err );
	if ( code != 0 ) return code;

	if( user && op ) return SLAP_SCHERR_CR_BAD_AUX;

	code = cr_insert(scr,err);
	return code;
}

#endif

int
cr_schema_info( Entry *e )
{
#ifdef SLAP_EXTENDED_SCHEMA
	struct berval	vals[2];
	ContentRule	*cr;

	AttributeDescription *ad_ditContentRules = slap_schema.si_ad_ditContentRules;

	vals[1].bv_val = NULL;

	for ( cr = cr_list; cr; cr = cr->scr_next ) {
		if ( ldap_contentrule2bv( &cr->scr_crule, vals ) == NULL ) {
			return -1;
		}

#if 0
		if( cr->scr_flags & SLAP_CR_HIDE ) continue;
#endif

#if 0
		Debug( LDAP_DEBUG_TRACE, "Merging cr [%ld] %s\n",
	       (long) vals[0].bv_len, vals[0].bv_val, 0 );
#endif
		attr_merge( e, ad_ditContentRules, vals );
		ldap_memfree( vals[0].bv_val );
	}
#endif
	return 0;
}
