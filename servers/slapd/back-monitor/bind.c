/* bind.c - monitor backend bind routine */
/* $OpenLDAP$ */
/* This work is part of OpenLDAP Software <http://www.openldap.org/>.
 *
 * Copyright 2001-2005 The OpenLDAP Foundation.
 * Portions Copyright 2001-2003 Pierangelo Masarati.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.
 *
 * A copy of this license is available in file LICENSE in the
 * top-level directory of the distribution or, alternatively, at
 * <http://www.OpenLDAP.org/license.html>.
 */
/* ACKNOWLEDGEMENTS:
 * This work was initially developed by Pierangelo Masarati for inclusion
 * in OpenLDAP Software.
 */

#include "portable.h"

#include <stdio.h>

#include <slap.h>
#include "back-monitor.h"

/*
 * At present, only rootdn can bind with simple bind
 */

int
monitor_back_bind( Operation *op, SlapReply *rs )
{
#if 0	/* not used yet */
	struct monitorinfo	*mi
		= (struct monitorinfo *) op->o_bd->be_private;
#endif

#ifdef NEW_LOGGING
	LDAP_LOG( BACK_MON, ENTRY, "monitor_back_bind: dn: %s.\n",
			op->o_req_dn.bv_val, 0, 0 );
#else
	Debug(LDAP_DEBUG_ARGS, "==> monitor_back_bind: dn: %s\n", 
			op->o_req_dn.bv_val, 0, 0 );
#endif
	
	if ( op->oq_bind.rb_method == LDAP_AUTH_SIMPLE 
			&& be_isroot_pw( op ) ) {
		ber_dupbv( &op->oq_bind.rb_edn, be_root_dn( op->o_bd ) );
		return( 0 );
	}

	rs->sr_err = LDAP_INVALID_CREDENTIALS;
	send_ldap_result( op, rs );

	return( 1 );
}

