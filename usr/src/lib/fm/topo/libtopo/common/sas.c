/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2019 Joyent, Inc.
 */

/*
 * The sas FMRI scheme is intended to be used in conjuction with a
 * digraph-based topology to represent a SAS fabric.
 *
 * There are four types of vertices in the topology:
 *
 * initiator
 * ---------
 * An initiator is a device on the SAS fabric that originates SCSI commands.
 * Typically this is a SAS host-bus adapter (HBA) which can be built onto the
 * system board or be part of a PCIe add-in card.
 *
 * XXX - add description of initiator node properties
 *
 * port
 * ----
 * A port is a logical construct that represents a grouping of one or more
 * PHYs.  A port with one PHY is known as a narrow port.  An example of a
 * narrow port would be the connection from an expander to a target device.
 * A port with more than one PHY is known as a wide port.  A typical example
 * of a wide port would be the connection from an initiator to an exander
 * (typically 4 or 8 PHYs wide).
 *
 * XXX - add description of port node properties
 *
 * target
 * ------
 * A target (or end-device) represents the device that is receiving
 * SCSI commands from the an initiator.   Examples include disks and SSDs as
 * well as SMP and SES management devices.  SES and SMP targets would
 * be connected to an expander.  Disk/SSD targets can be connected to an
 * expander or directly attached (via a narrow port) to an initiator.
 *
 * XXX - add description of target node properties
 *
 * XXX - It'd be really cool if we could check for a ZFS pool config and
 * try to match the target to a leaf vdev and include the zfs-scheme FMRI of
 * that vdev as a property on this node.
 *
 * XXX - Similarly, for disks/ssd's it'd be cool if we could a match the
 * target to a disk node in the hc-scheme topology and also add the
 * hc-scheme FMRI of that disk as a property on this node.  This one would
 * have to be a dynamic (propmethod) property because we'd need to walk
 * the hc-schem tree, which may not have been built when we're enumerating.
 *
 * expander
 * --------
 * An expander acts as both a port multiplexer and expander routing signals
 * between one or more initiators and one or more targets or possibly a
 * second layer of downstream expanders, depending on the size of the fabric.
 * The SAS specification optionally allows for up to two levels of expanders
 * between the initiator(s) and target(s).
 *
 * XXX - add description of expander node properties
 *
 * Version 0 sas FMRI scheme
 * -------------------------
 * Two types of resources can be represented in the sas FMRI scheme: paths
 * and pathnodes.  The "type" field in the authority portion of the FMRI
 * denotes whether the FMRI indentifies a pathnode or path:
 *
 * e.g.
 * sas://type=path/....
 * sas://type=pathnode/....
 *
 * Path
 * ----
 * The first resource type is a path, which represents a unique path from a
 * given initiator to a given target.  Hence, the first two node/instance pairs
 * are always an initiator and port and the last two pairs are always a port
 * and a target. In between there may be one or two sets of expander and port
 * pairs.
 *
 * e.g.
 * sas://<auth>/initiator=<inst>/<port>=<inst>/.../port=<inst>/target=<inst>
 *
 * Node instance numbers are based on the local SAS address of the underlying
 * component.  Each initiator, expander and target will have a unique[1] SAS
 * address.  And each port from an initiator or to a target will also have a
 * unique SAS address.  Note that expander ports are not individually
 * addressed, thus the instance number shall be the SAS address of the
 * expander, itself.
 *
 * [1] The SAS address will be unique within a given SAS fabric (domain)
 *
 * The nvlist representation of the FMRI consists of two nvpairs:
 *
 * name               type                   value
 * ----               ----                   -----
 * sas-fmri-version   DATA_TYPE_UINT8        0
 * sas-path           DATA_TYPE_NVLIST_ARRAY see below
 *
 * sas-path is an array of nvlists where each nvlist contains the following
 * nvpairs:
 *
 * name               type                   value
 * ----               ----                   -----
 * sas-name           DATA_TYPE_STRING       (initiator|port|expander|target)
 * sas-id             DATA_TYPE_UINT64       SAS address (see above)
 *
 *
 * Pathnode
 * --------
 * The second resource type in the sas FMRI scheme is a pathnode, which
 * represents a single node in the underlying graph topology.  In this form,
 * the FMRI consists of a single sas-name/sas-id pair.  In order to
 * differentiate the FMRIs for expander "port" nodes, which will share the same
 * SAS address as the expander, the range of PHYs associated with the port will
 * be added to the authority portion of the FMRI.  For expander ports that
 * connect directly to a target device, this will be a narrow port that spans a
 * single PHY:
 *
 * e.g.
 *
 * sas://type=pathnode:start-phy=0:end-phy=0/port=500304801861347f
 * sas://type=pathnode:start-phy=1:end-phy=1/port=500304801861347f
 *
 * For expander ports that connect to another expander, this will be a wide
 * port that will span a range of phys (typically 4 or 8 wide)
 *
 * e.g.
 *
 * sas://type=pathnode:start_phy=0:end_phy=7/port=500304801861347f
 *
 * Overview of SAS Topology Generation
 * -----------------------------------
 * The SAS topology is iterated using this high-level logic:
 *
 * 1) Each HBA is discovered.
 * 2) Each SAS port on each HBA is added to a list of ports that need to be
 *    further discovered (search_list).
 * 3) Create a digraph vertex for every device discovered. Some information is
 *    stored with each vertex like its local WWN, attached WWN, and port
 *    attributes.
 * 4) Iterate through each vertex drawing edges between all connected
 *    vertices. The connections are determined by matching local/attached WWN
 *    pairs. E.g. a disk with an attached WWN of 0xDEADBEEF and an HBA with a
 *    local WWN of 0xDEADBEEF are connected.
 */
#include <libnvpair.h>
#include <fm/topo_mod.h>

#include <sys/fm/protocol.h>
#include <sys/types.h>

#include <topo_digraph.h>
#include <topo_sas.h>
#include <topo_method.h>
#include <topo_subr.h>
#include "sas.h"

#include <smhbaapi.h>
#include <scsi/libsmp.h>

#include <libdevinfo.h>

static int sas_fmri_nvl2str(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int sas_fmri_str2nvl(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int sas_fmri_create(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int sas_dev_fmri(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int sas_hc_fmri(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

static const topo_method_t sas_methods[] = {
	{ TOPO_METH_NVL2STR, TOPO_METH_NVL2STR_DESC, TOPO_METH_NVL2STR_VERSION,
	    TOPO_STABILITY_INTERNAL, sas_fmri_nvl2str },
	{ TOPO_METH_STR2NVL, TOPO_METH_STR2NVL_DESC, TOPO_METH_STR2NVL_VERSION,
	    TOPO_STABILITY_INTERNAL, sas_fmri_str2nvl },
	{ TOPO_METH_FMRI, TOPO_METH_FMRI_DESC, TOPO_METH_FMRI_VERSION,
	    TOPO_STABILITY_INTERNAL, sas_fmri_create },
	{ TOPO_METH_SAS2DEV, TOPO_METH_SAS2DEV_DESC, TOPO_METH_SAS2DEV_VERSION,
	    TOPO_STABILITY_INTERNAL, sas_dev_fmri },
	{ TOPO_METH_SAS2HC, TOPO_METH_SAS2HC_DESC, TOPO_METH_SAS2HC_VERSION,
	    TOPO_STABILITY_INTERNAL, sas_hc_fmri },
	{ NULL }
};

static int sas_enum(topo_mod_t *, tnode_t *, const char *, topo_instance_t,
    topo_instance_t, void *, void *);
static void sas_release(topo_mod_t *, tnode_t *);

static const topo_modops_t sas_ops =
	{ sas_enum, sas_release };

static const topo_modinfo_t sas_info =
	{ "sas", FM_FMRI_SCHEME_SAS, SAS_VERSION, &sas_ops };

int
sas_init(topo_mod_t *mod, topo_version_t version)
{
	if (getenv("TOPOSASDEBUG"))
		topo_mod_setdebug(mod);
	topo_mod_dprintf(mod, "initializing sas builtin\n");

	if (version != SAS_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (topo_mod_register(mod, &sas_info, TOPO_VERSION) != 0) {
		topo_mod_dprintf(mod, "failed to register sas_info: "
		    "%s\n", topo_mod_errmsg(mod));
		return (-1);
	}

	return (0);
}

void
sas_fini(topo_mod_t *mod)
{
	topo_mod_unregister(mod);
}

static topo_pgroup_info_t protocol_pgroup = {
	TOPO_PGROUP_PROTOCOL,
	TOPO_STABILITY_PRIVATE,
	TOPO_STABILITY_PRIVATE,
	1
};

struct sas_phy_info {
	uint32_t	start_phy;
	uint32_t	end_phy;
};

static topo_vertex_t *
sas_create_vertex(topo_mod_t *mod, const char *name, topo_instance_t inst,
    struct sas_phy_info *phyinfo)
{
	topo_vertex_t *vtx;
	tnode_t *tn;
	topo_pgroup_info_t pgi;
	int err;
	nvlist_t *auth = NULL, *fmri = NULL;

	pgi.tpi_namestab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_datastab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_version = TOPO_VERSION;
	if (strcmp(name, TOPO_VTX_EXPANDER) == 0)
		pgi.tpi_name = TOPO_PGROUP_EXPANDER;
	else if (strcmp(name, TOPO_VTX_INITIATOR) == 0)
		pgi.tpi_name = TOPO_PGROUP_INITIATOR;
	else if (strcmp(name, TOPO_VTX_PORT) == 0)
		pgi.tpi_name = TOPO_PGROUP_SASPORT;
	else if (strcmp(name, TOPO_VTX_TARGET) == 0)
		pgi.tpi_name = TOPO_PGROUP_TARGET;
	else {
		topo_mod_dprintf(mod, "invalid vertex name: %s", name);
		return (NULL);
	}

	if ((vtx = topo_vertex_new(mod, name, inst)) == NULL) {
		/* errno set */
		topo_mod_dprintf(mod, "failed to create vertex: "
		    "%s=%" PRIx64 "", name, inst);
		return (NULL);
	}
	tn = topo_vertex_node(vtx);

	if (topo_mod_nvalloc(mod, &auth, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(auth, FM_FMRI_SAS_TYPE,
	    FM_FMRI_SAS_TYPE_PATHNODE) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	if (strcmp(name, TOPO_VTX_PORT) == 0 && phyinfo != NULL) {
		/*
		 * if (phyinfo == NULL) {
		 *	goto err;
		 * }
		 */
		if (nvlist_add_uint32(auth, FM_FMRI_SAS_START_PHY,
		    phyinfo->start_phy) != 0 ||
		    nvlist_add_uint32(auth, FM_FMRI_SAS_END_PHY,
		    phyinfo->end_phy) != 0) {
			(void) topo_mod_seterrno(mod, EMOD_NOMEM);
			topo_mod_dprintf(mod, "failed to construct auth for "
			    "node: %s=%" PRIx64, name, inst);
			goto err;
		}
	}
	if ((fmri = topo_mod_sasfmri(mod, FM_SAS_SCHEME_VERSION, name, inst,
	    auth)) == NULL) {
		/* errno set */
		topo_mod_dprintf(mod, "failed to construct FMRI for "
		    "%s=%" PRIx64 ": %s", name, inst, topo_strerror(err));
		goto err;
	}
	if (topo_pgroup_create(tn, &pgi, &err) != 0) {
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod, "failed to create %s propgroup on "
		    "%s=%" PRIx64 ": %s", pgi.tpi_name, name, inst,
		    topo_strerror(err));
		goto err;
	}
	if (topo_pgroup_create(tn, &protocol_pgroup, &err) < 0 ||
	    topo_prop_set_fmri(tn, TOPO_PGROUP_PROTOCOL, TOPO_PROP_RESOURCE,
	    TOPO_PROP_IMMUTABLE, fmri, &err) < 0) {
		(void) topo_mod_seterrno(mod, err);
		topo_mod_dprintf(mod, "failed to create %s propgroup on "
		    "%s=%" PRIx64 ": %s", TOPO_PGROUP_PROTOCOL, name, inst,
		    topo_strerror(err));
		goto err;
	}

	return (vtx);
err:
	nvlist_free(auth);
	topo_vertex_destroy(mod, vtx);
	return (NULL);
}

static int
fake_enum(topo_mod_t *mod, tnode_t *rnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *notused1, void *notused2)
{
	/*
	 * XXX - this code simply hardcodes a minimal topology in order to
	 * facilitate early unit testing of the topo_digraph code.  This
	 * will be replaced by proper code that will discover and dynamically
	 * enumerate the SAS fabric(s).
	 */
	topo_vertex_t *ini, *ini_p1, *exp_in1, *exp, *exp_out1, *exp_out2,
	    *tgt1_p1, *tgt2_p1, *tgt1, *tgt2;

	topo_vertex_t *exp_out3, *tgt3_p1, *tgt3, *exp2, *exp2_in1, *exp2_out1;

	tnode_t *tn;
	int err;

	uint64_t ini_addr = 0x5003048023567a00;
	uint64_t exp_addr = 0x500304801861347f;
	uint64_t tg1_addr = 0x5000cca2531b1025;
	uint64_t tg2_addr = 0x5000cca2531a41b9;

	uint64_t tg3_addr = 0xDEADBEED;
	uint64_t exp2_addr = 0xDEADBEEF;
	struct sas_phy_info phyinfo;

	/*
	 * Create vertices for an initiator and one outgoing port
	 */
	if ((ini = sas_create_vertex(mod, TOPO_VTX_INITIATOR, ini_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(ini);
	if (topo_prop_set_string(tn, TOPO_PGROUP_INITIATOR,
	    TOPO_PROP_INITIATOR_MANUF, TOPO_PROP_IMMUTABLE, "LSI",
	    &err) != 0 ||
	    topo_prop_set_string(tn, TOPO_PGROUP_INITIATOR,
	    TOPO_PROP_INITIATOR_MODEL, TOPO_PROP_IMMUTABLE, "LSI3008-IT",
	    &err) != 0 ||
	    topo_prop_set_string(tn, TOPO_PGROUP_INITIATOR,
	    TOPO_PROP_INITIATOR_SERIAL, TOPO_PROP_IMMUTABLE, "LSI23098420374",
	    &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 7;
	if ((ini_p1 = sas_create_vertex(mod, TOPO_VTX_PORT, ini_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(ini_p1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5003048023567a00, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}

	if (topo_edge_new(mod, ini, ini_p1) != 0)
		return (-1);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 7;
	if ((exp_in1 = sas_create_vertex(mod, TOPO_VTX_PORT, exp_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp_in1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5003048023567a00, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, ini_p1, exp_in1) != 0)
		return (-1);

	if ((exp = sas_create_vertex(mod, TOPO_VTX_EXPANDER, exp_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp);
	if (topo_prop_set_string(tn, TOPO_PGROUP_EXPANDER,
	    TOPO_PROP_EXPANDER_DEVFSNAME, TOPO_PROP_IMMUTABLE,
	    "/dev/smp/expd0", &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp_in1, exp) != 0)
		return (-1);

	phyinfo.start_phy = 8;
	phyinfo.end_phy = 8;
	if ((exp_out1 = sas_create_vertex(mod, TOPO_VTX_PORT, exp_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp_out1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2531a41b9, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp, exp_out1) != 0)
		return (-1);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 0;
	if ((tgt1_p1 = sas_create_vertex(mod, TOPO_VTX_PORT, tg1_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt1_p1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2531a41b9, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp_out1, tgt1_p1) != 0)
		return (-1);

	if ((tgt1 = sas_create_vertex(mod, TOPO_VTX_TARGET, tg1_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt1);
	if (topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MANUF, TOPO_PROP_IMMUTABLE, "HGST",
	    &err) != 0 ||
	    topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MODEL, TOPO_PROP_IMMUTABLE, "HUH721212AL4200",
	    &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}

	if (topo_edge_new(mod, tgt1_p1, tgt1) != 0)
		return (-1);

	phyinfo.start_phy = 9;
	phyinfo.end_phy = 9;
	if ((exp_out2 = sas_create_vertex(mod, TOPO_VTX_PORT, exp_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp_out2);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2531b1025, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp, exp_out2) != 0)
		return (-1);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 0;
	if ((tgt2_p1 = sas_create_vertex(mod, TOPO_VTX_PORT, tg2_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt2_p1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2531b1025, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp_out2, tgt2_p1) != 0)
		return (-1);

	if ((tgt2 = sas_create_vertex(mod, TOPO_VTX_TARGET, tg2_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt2);
	if (topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MANUF, TOPO_PROP_IMMUTABLE, "HGST",
	    &err) != 0 ||
	    topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MODEL, TOPO_PROP_IMMUTABLE, "HUH721212AL4200",
	    &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}

	if (topo_edge_new(mod, tgt2_p1, tgt2) != 0)
		return (-1);

	/* Attach to second expander with one target device */
	phyinfo.start_phy = 10;
	phyinfo.end_phy = 17;
	if ((exp_out3 = sas_create_vertex(mod, TOPO_VTX_PORT, exp_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp_out3);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801e84c7ff, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp, exp_out3) != 0)
		return (-1);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 7;
	if ((exp2_in1 = sas_create_vertex(mod, TOPO_VTX_PORT, exp2_addr,
	    &phyinfo)) == NULL) {
		return (-1);
	}

	tn = topo_vertex_node(exp2_in1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801e84c7ff, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801861347f, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp_out3, exp2_in1) != 0)
		return (-1);

	if ((exp2 = sas_create_vertex(mod, TOPO_VTX_EXPANDER, exp2_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp2);
	if (topo_prop_set_string(tn, TOPO_PGROUP_EXPANDER,
	    TOPO_PROP_EXPANDER_DEVFSNAME, TOPO_PROP_IMMUTABLE,
	    "/dev/smp/expd1", &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp2_in1, exp2) != 0)
		return (-1);

	phyinfo.start_phy = 8;
	phyinfo.end_phy = 8;
	if ((exp2_out1 = sas_create_vertex(mod, TOPO_VTX_PORT, exp2_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(exp2_out1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801e84c7ff, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2530f9c55, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp2, exp2_out1) != 0)
		return (-1);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = 0;
	if ((tgt3_p1 = sas_create_vertex(mod, TOPO_VTX_PORT, tg3_addr,
	    &phyinfo)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt3_p1);
	if (topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_LOCAL_ADDR, TOPO_PROP_IMMUTABLE,
	    0x5000cca2530f9c55, &err) != 0 ||
	    topo_prop_set_uint64(tn, TOPO_PGROUP_SASPORT,
	    TOPO_PROP_SASPORT_ATTACH_ADDR, TOPO_PROP_IMMUTABLE,
	    0x500304801e84c7ff, &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}
	if (topo_edge_new(mod, exp2_out1, tgt3_p1) != 0)
		return (-1);

	if ((tgt3 = sas_create_vertex(mod, TOPO_VTX_TARGET, tg3_addr,
	    NULL)) == NULL)
		return (-1);

	tn = topo_vertex_node(tgt3);
	if (topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MANUF, TOPO_PROP_IMMUTABLE, "HGST",
	    &err) != 0 ||
	    topo_prop_set_string(tn, TOPO_PGROUP_TARGET,
	    TOPO_PROP_TARGET_MODEL, TOPO_PROP_IMMUTABLE, "HUH721212AL4200",
	    &err) != 0) {
		topo_mod_dprintf(mod, "Failed to set props on %s=%" PRIx64,
		    topo_node_name(tn), topo_node_instance(tn));
		return (-1);
	}

	if (topo_edge_new(mod, tgt3_p1, tgt3) != 0)
		return (-1);

	return (0);
}

static uint64_t
wwn_to_uint64(HBA_WWN wwn)
{
	uint64_t res;
	(void) memcpy(&res, &wwn, sizeof (uint64_t));
	return (ntohll(res));
}

typedef struct sas_port {
	topo_list_t		sp_list;
	uint64_t		sp_att_wwn;
	topo_vertex_t		*sp_vtx; /* port pointer */
	boolean_t		sp_is_expander;
	boolean_t		sp_has_hba_connection;
} sas_port_t;

typedef struct sas_vtx_search {
	topo_instance_t	inst;
	const char	*name;
	topo_list_t	*result_list;
} sas_vtx_search_t;

typedef struct sas_vtx {
	topo_list_t	tds_list;
	topo_vertex_t	*tds_vtx;
} sas_vtx_t;

/* Finds vertices matching th given tn_instance and tn_name. */
int
sas_vtx_match(topo_hdl_t *thp, topo_vertex_t *vtx, boolean_t last,
    void *arg)
{
	sas_vtx_search_t *search = arg;
	sas_vtx_t *res = NULL;
	tnode_t *node = topo_vertex_node(vtx);

	if (node->tn_instance == search->inst &&
	    strcmp(node->tn_name, search->name) == 0) {
		res = topo_hdl_zalloc(thp, sizeof (sas_vtx_t));
		if (res != NULL) {
			res->tds_vtx = vtx;
			topo_list_append(search->result_list, res);
		}
	}
	return (TOPO_WALK_NEXT);
}

static uint_t
sas_find_connected_vtx(topo_mod_t *mod, uint64_t att_wwn, uint64_t search_wwn,
    const char *vtx_type, topo_list_t *res_list)
{
	topo_list_t *vtx_list = topo_mod_zalloc(mod, sizeof (topo_list_t));
	sas_vtx_search_t search;
	search.inst = search_wwn;
	search.name = vtx_type;
	search.result_list = vtx_list;

	uint_t nfound = 0;

	(void) topo_vertex_iter(mod->tm_hdl, topo_digraph_get(mod->tm_hdl,
	    FM_FMRI_SCHEME_SAS), sas_vtx_match, &search);

	for (sas_vtx_t *res = topo_list_next(vtx_list);
	    res != NULL; res = topo_list_next(res)) {
		if (strcmp(vtx_type, TOPO_VTX_PORT) == 0 && att_wwn != 0) {
			/* The caller is looking for a specific linkage. */
			sas_port_t *res_port = topo_node_getspecific(
			    topo_vertex_node(res->tds_vtx));

			if ((res_port != NULL &&
			    res_port->sp_att_wwn != att_wwn) ||
			    res->tds_vtx->tvt_nincoming != 0)
				continue;

			sas_vtx_t *vtx = topo_mod_zalloc(
			    mod, sizeof (sas_vtx_t));
			vtx->tds_vtx = res->tds_vtx;
			topo_list_append(res_list, vtx);
			nfound++;
		} else if (strcmp(vtx_type, TOPO_VTX_EXPANDER) == 0) {
			/*
			 * The caller is looking for anything that matches
			 * search_wwn. There should only be one expander vtx
			 * matching this description.
			 */
			sas_vtx_t *vtx = topo_mod_zalloc(
			    mod, sizeof (sas_vtx_t));
			vtx->tds_vtx = res->tds_vtx;
			topo_list_append(res_list, vtx);
			nfound++;
		}
	}

	/* Clean up all the garbage used by the search routine. */
	for (sas_vtx_t *res = topo_list_next(vtx_list);
	    res != NULL; res = topo_list_next(res)) {
		topo_mod_free(mod, res, sizeof (sas_vtx_t));
	}
	topo_mod_free(mod, vtx_list, sizeof (topo_list_t));

	return (nfound);
}

static int
sas_expander_discover(topo_mod_t *mod, const char *smp_path,
    topo_list_t *expd_list)
{
	int ret = 0;
	int i;
	uint8_t *smp_resp, num_phys;
	uint64_t expd_addr;
	size_t smp_resp_len;

	smp_target_def_t *tdef = NULL;
	smp_target_t *tgt = NULL;
	smp_action_t *axn = NULL;
	smp_report_general_resp_t *report_resp = NULL;

	smp_function_t func;
	smp_result_t result;

	topo_vertex_t *expd_vtx = NULL;
	struct sas_phy_info phyinfo;
	sas_port_t *port_info = NULL;

	tdef = (smp_target_def_t *)topo_mod_zalloc(mod,
	    sizeof (smp_target_def_t));

	tdef->std_def = smp_path;

	if ((tgt = smp_open(tdef)) == NULL) {
		ret = -1;
		topo_mod_dprintf(mod, "failed to open SMP target\n");
		goto done;
	}

	axn = smp_action_alloc(func, tgt, 0);

	if (smp_exec(axn, tgt) != 0) {
		ret = -1;
		goto done;
	}

	smp_action_get_response(axn, &result, (void **) &smp_resp,
	    &smp_resp_len);
	if (result != SMP_RES_FUNCTION_ACCEPTED) {
		ret = -1;
		goto done;
	}

	report_resp = (smp_report_general_resp_t *)smp_resp;
	num_phys = report_resp->srgr_number_of_phys;
	expd_addr = ntohll(report_resp->srgr_enclosure_logical_identifier);
	smp_action_free(axn);

	phyinfo.start_phy = 0;
	phyinfo.end_phy = (phyinfo.start_phy + num_phys) - (num_phys - 1);
	if ((expd_vtx = sas_create_vertex(mod, TOPO_VTX_EXPANDER, expd_addr,
	    &phyinfo)) == NULL) {
		ret = -1;
		goto done;
	}
	port_info = topo_mod_zalloc(mod, sizeof (sas_port_t));
	port_info->sp_vtx = expd_vtx;
	port_info->sp_is_expander = B_TRUE;
	topo_node_setspecific(topo_vertex_node(expd_vtx), port_info);

	boolean_t wide_port_discovery = B_FALSE;
	uint64_t wide_port_att_wwn;
	struct sas_phy_info wide_port_phys;
	bzero(&wide_port_phys, sizeof (struct sas_phy_info));
	for (i = 0; i < num_phys; i++) {
		smp_discover_req_t *disc_req = NULL;
		smp_discover_resp_t *disc_resp = NULL;

		func = SMP_FUNC_DISCOVER;
		axn = smp_action_alloc(func, tgt, 0);
		smp_action_get_request(axn, (void **) &disc_req, NULL);
		disc_req->sdr_phy_identifier = i;

		if (smp_exec(axn, tgt) != 0) {
			topo_mod_dprintf(mod, "smp_exec failed\n");
			goto done;
		}

		smp_action_get_response(axn, &result, (void **) &smp_resp,
		    &smp_resp_len);
		disc_resp = (smp_discover_resp_t *)smp_resp;
		if (result != SMP_RES_FUNCTION_ACCEPTED &&
		    result != SMP_RES_PHY_VACANT) {
			topo_mod_dprintf(mod, "function not accepted\n");
			goto done;
		}

		if (result == SMP_RES_PHY_VACANT) {
			continue;
		}

		if (disc_resp->sdr_attached_device_type == SMP_DEV_SAS_SATA &&
		    (disc_resp->sdr_attached_ssp_target ||
		    disc_resp->sdr_attached_stp_target) &&
		    disc_resp->sdr_connector_type == 0x20 &&
		    !disc_resp->sdr_attached_smp_target) {
			/*
			 * 0x20 == expander backplane receptacle.
			 * XXX We should use ses_sasconn_type_t enum from
			 * ses2.h. Acceptable values (as of SES-3): 0x20 - 0x2F.
			 */

			/*
			 * The current phy cannot be part of a wide
			 * port, so the previous wide port discovery
			 * effort must be committed.
			 */
			if (wide_port_discovery) {
				wide_port_discovery = B_FALSE;
				sas_port_t *expd_port = topo_mod_zalloc(
				    mod, sizeof (sas_port_t));

				expd_port->sp_att_wwn =
				    htonll(wide_port_att_wwn);
				expd_port->sp_is_expander = B_TRUE;
				if ((expd_port->sp_vtx =
				    sas_create_vertex(mod, TOPO_VTX_PORT,
				    expd_addr, &wide_port_phys)) == NULL) {
					topo_mod_free(mod, expd_port,
					    sizeof (sas_port_t));
					ret = -1;
					goto done;
				}
				topo_list_append(expd_list, expd_port);
				topo_node_setspecific(
				    topo_vertex_node(expd_port->sp_vtx),
				    expd_port);
			}
			topo_vertex_t *ex_pt_vtx, *port_vtx, *tgt_vtx;

			/* Phy info for expander port is the expander's phy. */
			phyinfo.start_phy = disc_resp->sdr_phy_identifier;
			phyinfo.end_phy = disc_resp->sdr_phy_identifier;
			if ((ex_pt_vtx = sas_create_vertex(mod, TOPO_VTX_PORT,
			    ntohll(disc_resp->sdr_sas_addr),
			    &phyinfo)) == NULL) {
				ret = -1;
				goto done;
			}

			if (topo_edge_new(mod, expd_vtx, ex_pt_vtx) != 0) {
				topo_vertex_destroy(mod, ex_pt_vtx);
				ret = -1;
				goto done;
			}

			/*
			 * Phy info for attached device port is the device's
			 * internal phy.
			 */
			phyinfo.start_phy =
			    disc_resp->sdr_attached_phy_identifier;
			phyinfo.end_phy =
			    disc_resp->sdr_attached_phy_identifier;
			if ((port_vtx = sas_create_vertex(mod, TOPO_VTX_PORT,
			    ntohll(disc_resp->sdr_attached_sas_addr),
			    &phyinfo)) == NULL) {
				topo_vertex_destroy(mod, ex_pt_vtx);
				ret = -1;
				goto done;
			}

			if (topo_edge_new(mod, ex_pt_vtx, port_vtx) != 0) {
				topo_vertex_destroy(mod, ex_pt_vtx);
				topo_vertex_destroy(mod, port_vtx);
				ret = -1;
				goto done;
			}

			/* This is a target disk. */
			if ((tgt_vtx = sas_create_vertex(mod, TOPO_VTX_TARGET,
			    ntohll(disc_resp->sdr_attached_sas_addr),
			    &phyinfo)) == NULL) {
				topo_vertex_destroy(mod, ex_pt_vtx);
				topo_vertex_destroy(mod, port_vtx);
				ret = -1;
				goto done;
			}

			if (topo_edge_new(mod, port_vtx, tgt_vtx) != 0) {
				topo_vertex_destroy(mod, ex_pt_vtx);
				topo_vertex_destroy(mod, port_vtx);
				topo_vertex_destroy(mod, tgt_vtx);
				ret = -1;
				goto done;
			}
		} else if (disc_resp->sdr_attached_device_type
		    == SMP_DEV_EXPANDER ||
		    (disc_resp->sdr_attached_ssp_initiator ||
		    disc_resp->sdr_attached_stp_initiator ||
		    disc_resp->sdr_attached_smp_initiator)) {
			/*
			 * This phy is for another 'complicated' device like an
			 * expander or an HBA. This phy may be in a wide port
			 * configuration.
			 *
			 * To discover wide ports we allow the phy discovery
			 * loop to continue to run. When this block
			 * first encounters a possibly wide port it sets the
			 * start phy to the current phy, and it is not modified
			 * again.
			 *
			 * Each time this block finds the same attached SAS
			 * address we update the end phy identifier to be the
			 * current phy.
			 *
			 * Once the phy discovery loop finds a new attached SAS
			 * address we know that the (possibly) wide port is done
			 * being discovered and it should be 'committed.'
			 */

			/*
			 * The current phy cannot be part of a wide
			 * port, so the previous wide port discovery
			 * effort must be committed.
			 */
			if (disc_resp->sdr_attached_sas_addr
			    != wide_port_att_wwn && wide_port_discovery) {
				wide_port_discovery = B_FALSE;
				sas_port_t *expd_port = topo_mod_zalloc(
				    mod, sizeof (sas_port_t));

				expd_port->sp_att_wwn =
				    htonll(wide_port_att_wwn);
				expd_port->sp_is_expander = B_TRUE;
				if ((expd_port->sp_vtx =
				    sas_create_vertex(mod, TOPO_VTX_PORT,
				    expd_addr, &wide_port_phys)) == NULL) {
					topo_mod_free(mod, expd_port,
					    sizeof (sas_port_t));
					ret = -1;
					goto done;
				}
				topo_node_setspecific(
				    topo_vertex_node(expd_port->sp_vtx),
				    expd_port);
				topo_list_append(expd_list, expd_port);
			}

			if (!wide_port_discovery) {
				/* New wide port discovery run. */
				wide_port_discovery = B_TRUE;
				wide_port_phys.start_phy =
				    disc_resp->sdr_phy_identifier;
				wide_port_att_wwn =
				    disc_resp->sdr_attached_sas_addr;
			}

			wide_port_phys.end_phy =
			    disc_resp->sdr_phy_identifier;
		}

		smp_action_free(axn);
	}

done:
	smp_action_free(axn);
	smp_close(tgt);
	topo_mod_free(mod, tdef, sizeof (smp_target_def_t));
	return (ret);
}

typedef struct sas_topo_iter {
	topo_mod_t	*sas_mod;
	uint64_t	sas_search_wwn;
	topo_list_t	*sas_expd_list;
} sas_topo_iter_t;

/* Responsible for creating links from HBA -> fanout expanders. */
static int
sas_connect_hba(topo_hdl_t *hdl, topo_edge_t *edge, boolean_t last, void* arg)
{
	sas_topo_iter_t *iter = arg;
	tnode_t *node = topo_vertex_node(edge->tve_vertex);
	sas_port_t *hba_port = topo_node_getspecific(node);
	topo_vertex_t *expd_port_vtx = NULL;
	topo_vertex_t *expd_vtx = NULL;
	sas_port_t *expd_port = NULL;

	topo_list_t *vtx_list = topo_mod_zalloc(
	    iter->sas_mod, sizeof (topo_list_t));

	if (strcmp(node->tn_name, TOPO_VTX_PORT) == 0 &&
	    edge->tve_vertex->tvt_noutgoing == 0) {
		/*
		 * This is a port vtx that isn't connected to anything. We need
		 * to:
		 * - find the expander port that this hba port is connected to.
		 * - if not already connected, connect the expander port to the
		 *   expander itself.
		 */
		uint_t nfound = sas_find_connected_vtx(iter->sas_mod,
		    node->tn_instance, hba_port->sp_att_wwn, TOPO_VTX_PORT,
		    vtx_list);

		/*
		 * XXX need to match up the phys in case this expd is
		 * connected to more than one hba. In that case nfound should be
		 * > 1.
		 */
		if (nfound > 1)
			goto out;
		sas_vtx_t *vtx = topo_list_next(vtx_list);
		expd_port_vtx = vtx->tds_vtx;
		if (expd_port_vtx == NULL ||
		    topo_edge_new(iter->sas_mod, edge->tve_vertex,
		    expd_port_vtx) != 0) {
			goto out;
		}
		topo_list_delete(vtx_list, vtx);
		topo_mod_free(iter->sas_mod, vtx, sizeof (sas_vtx_t));

		nfound = sas_find_connected_vtx(iter->sas_mod,
		    0, /* expd vtx doesn't have an attached SAS addr */
		    topo_vertex_node(expd_port_vtx)->tn_instance,
		    TOPO_VTX_EXPANDER, vtx_list);

		/* There should only be one expander vtx with this SAS addr. */
		if (nfound > 1)
			goto out;
		expd_vtx =
		    ((sas_vtx_t *)topo_list_next(vtx_list))->tds_vtx;
		if (expd_vtx == NULL ||
		    topo_edge_new(iter->sas_mod, expd_port_vtx,
		    expd_vtx) != 0) {
			goto out;
		}
		expd_port = topo_node_getspecific(topo_vertex_node(expd_vtx));
		expd_port->sp_has_hba_connection = B_TRUE;
	}

out:
	for (sas_vtx_t *vtx = topo_list_next(vtx_list); vtx != NULL;
	    vtx = topo_list_next(vtx)) {
		topo_mod_free(iter->sas_mod, vtx, sizeof (sas_vtx_t));
	}
	topo_mod_free(iter->sas_mod, vtx_list, sizeof (topo_list_t));
	return (TOPO_WALK_NEXT);
}

static int
sas_expd_interconnect(topo_hdl_t *hdl, topo_vertex_t *vtx,
    sas_topo_iter_t *iter)
{
	int ret = 0;
	tnode_t *node = topo_vertex_node(vtx);
	topo_list_t *list = topo_mod_zalloc(
	    iter->sas_mod, sizeof (topo_list_t));
	sas_port_t *port = topo_node_getspecific(node);
	topo_vertex_t *port_vtx = NULL;

	uint_t nfound = sas_find_connected_vtx(iter->sas_mod, node->tn_instance,
	    port->sp_att_wwn, TOPO_VTX_PORT, list);

	if (nfound == 0) {
		ret = -1;
		goto out;
	}

	/*
	 * XXX make this work for multiple expd <-> expd connections. Likely
	 * need to compare local/att port phys.
	 */
	if ((port_vtx = ((sas_vtx_t *)topo_list_next(list))->tds_vtx) == NULL) {
		ret = -1;
		goto out;
	}

	if (topo_edge_new(iter->sas_mod, vtx, port_vtx) != 0) {
		goto out;
	}

out:
	for (sas_vtx_t *disc_vtx = topo_list_next(list); disc_vtx != NULL;
	    disc_vtx = topo_list_next(disc_vtx)) {
		topo_mod_free(iter->sas_mod, disc_vtx, sizeof (sas_vtx_t));
	}
	topo_mod_free(iter->sas_mod, list, sizeof (topo_list_t));
	return (ret);
}

/*
 * This routine is responsible for connecting expander port vertices to their
 * associated expander. The trick is getting the 'direction' of the connection
 * correct since SMP does not provide this information.
 */
static int
sas_connect_expd(topo_hdl_t *hdl, topo_vertex_t *vtx, sas_topo_iter_t *iter)
{
	int ret = 0;
	tnode_t *node = topo_vertex_node(vtx);
	topo_vertex_t *expd_vtx = NULL;
	sas_port_t *disc_expd = NULL;

	topo_list_t *list = topo_mod_zalloc(
	    iter->sas_mod, sizeof (topo_list_t));

	/* Find the port's corresponding expander vertex. */
	uint_t nfound = sas_find_connected_vtx(iter->sas_mod, 0,
	    node->tn_instance, TOPO_VTX_EXPANDER, list);
	if (nfound == 0) {
		ret = -1;
		goto out;
	}

	if ((expd_vtx = ((sas_vtx_t *)topo_list_next(list))->tds_vtx) == NULL) {
		ret = -1;
		goto out;
	}

	disc_expd = topo_node_getspecific(topo_vertex_node(expd_vtx));
	/*
	 * XXX This assumes only one of the expanders is connected to an HBA.
	 * It should be possible for two expanders to both be connected to HBAs
	 * and also connected to each other. However, if we do this today the
	 * path finding logic in topo ends up doing infinite recursion trying
	 * to find targets.
	 */
	if (!disc_expd->sp_has_hba_connection) {
		if (topo_edge_new(iter->sas_mod, vtx, expd_vtx) != 0) {
			goto out;
		}
	} else {
		if (topo_edge_new(iter->sas_mod, expd_vtx, vtx) != 0) {
			goto out;
		}
	}

out:
	for (sas_vtx_t *disc_vtx = topo_list_next(list); disc_vtx != NULL;
	    disc_vtx = topo_list_next(disc_vtx)) {
		topo_mod_free(iter->sas_mod, disc_vtx, sizeof (sas_vtx_t));
	}
	topo_mod_free(iter->sas_mod, list, sizeof (topo_list_t));
	return (ret);
}

static int
sas_vtx_final_pass(topo_hdl_t *hdl, topo_vertex_t *vtx, boolean_t last,
    void *arg)
{
	sas_topo_iter_t *iter = arg;
	tnode_t *node = topo_vertex_node(vtx);
	sas_port_t *port = topo_node_getspecific(node);

	if (node != NULL && strcmp(node->tn_name, TOPO_VTX_PORT) == 0) {
		/*
		 * Connect this outbound port to another expander's inbound
		 * port.
		 */
		if (port != NULL && port->sp_vtx->tvt_noutgoing == 0 &&
		    port->sp_is_expander) {
			(void) sas_expd_interconnect(hdl, vtx, iter);
		}
	}

	return (0);
}

static int
sas_vtx_iter(topo_hdl_t *hdl, topo_vertex_t *vtx, boolean_t last, void *arg)
{
	sas_topo_iter_t *iter = arg;
	tnode_t *node = topo_vertex_node(vtx);
	sas_port_t *port = topo_node_getspecific(node);

	if (strcmp(node->tn_name, TOPO_VTX_INITIATOR) == 0) {
		(void) topo_edge_iter(hdl, vtx, sas_connect_hba, iter);
	} else if (strcmp(node->tn_name, TOPO_VTX_PORT) == 0) {

		/* Connect the port to its expander vtx. */
		if (port != NULL && port->sp_is_expander &&
		    port->sp_vtx->tvt_nincoming == 0)
			(void) sas_connect_expd(hdl, vtx, iter);

	}
	return (TOPO_WALK_NEXT);
}

static int
sas_enum(topo_mod_t *mod, tnode_t *rnode, const char *name,
    topo_instance_t min, topo_instance_t max, void *notused1, void *notused2)
{
	if (topo_method_register(mod, rnode, sas_methods) != 0) {
		topo_mod_dprintf(mod, "failed to register scheme methods");
		/* errno set */
		return (-1);
	}

	if (getenv("TOPO_SASNOENUM"))
		return (0);

	if (getenv("SAS_FAKE_ENUM"))
		return (fake_enum(mod, rnode, name, min, max, notused1,
		    notused2));

	int ret = 0;

	di_node_t root;
	di_node_t smp;
	const char *smp_path = NULL;
	sas_port_t *sas_hba_port = NULL;
	topo_list_t *expd_list = topo_mod_zalloc(mod, sizeof (topo_list_t));
	topo_list_t *hba_list = topo_mod_zalloc(mod, sizeof (topo_list_t));

	/* Begin by discovering all HBAs and their immediate ports. */
	HBA_HANDLE handle;
	SMHBA_ADAPTERATTRIBUTES attrs;
	HBA_UINT32 num_ports, num_adapters;
	char aname[256];
	uint64_t hba_wwn;

	if ((ret = HBA_LoadLibrary()) != HBA_STATUS_OK) {
		return (ret);
	}

	num_adapters = HBA_GetNumberOfAdapters();
	if (num_adapters == 0) {
		ret = -1;
		goto done;
	}

	for (int i = 0; i < num_adapters; i++) {
		topo_vertex_t *initiator = NULL;
		if ((ret = HBA_GetAdapterName(i, aname)) != 0) {
			topo_mod_dprintf(mod, "failed to get adapter name\n");
			goto done;
		}

		if ((handle = HBA_OpenAdapter(aname)) == 0) {
			topo_mod_dprintf(mod, "failed to open adapter\n");
			goto done;
		}

		if ((ret = SMHBA_GetAdapterAttributes(handle, &attrs)) !=
		    HBA_STATUS_OK) {
			topo_mod_dprintf(mod, "failed to get adapter attrs\n");
			goto done;
		}

		if ((ret = SMHBA_GetNumberOfPorts(handle, &num_ports)) !=
		    HBA_STATUS_OK) {
			topo_mod_dprintf(mod, "failed to get num ports\n");
			goto done;
		}
		for (int j = 0; j < num_ports; j++) {
			SMHBA_PORTATTRIBUTES *attrs = NULL;
			SMHBA_SAS_PORT *sas_port;
			SMHBA_SAS_PHY phy_attrs;
			HBA_UINT32 num_phys;
			struct sas_phy_info phyinfo;

			topo_vertex_t *hba_port = NULL;

			attrs = topo_mod_zalloc(mod,
			    sizeof (SMHBA_PORTATTRIBUTES));
			sas_port = topo_mod_zalloc(mod,
			    sizeof (SMHBA_SAS_PORT));
			attrs->PortSpecificAttribute.SASPort = sas_port;

			if ((ret = SMHBA_GetAdapterPortAttributes(
			    handle, j, attrs)) != HBA_STATUS_OK) {
				topo_mod_free(mod, attrs,
				    sizeof (SMHBA_PORTATTRIBUTES));
				topo_mod_free(mod, sas_port,
				    sizeof (SMHBA_SAS_PORT));
				goto done;
			}
			hba_wwn = wwn_to_uint64(sas_port->LocalSASAddress);
			num_phys = sas_port->NumberofPhys;

			/* Calculate the beginning and end phys for this port */
			for (int k = 0; k < num_phys; k++) {
				if ((ret = SMHBA_GetSASPhyAttributes(handle,
				    j, k, &phy_attrs)) != HBA_STATUS_OK) {
					topo_mod_free(mod, attrs,
					    sizeof (SMHBA_PORTATTRIBUTES));
					topo_mod_free(mod, sas_port,
					    sizeof (SMHBA_SAS_PORT));
					goto done;
				}

				if (k == 0) {
					phyinfo.start_phy =
					    phy_attrs.PhyIdentifier;
					continue;
				}
				phyinfo.end_phy = phy_attrs.PhyIdentifier;
			}

			if ((hba_port = sas_create_vertex(mod, TOPO_VTX_PORT,
			    hba_wwn, &phyinfo)) == NULL) {
				topo_mod_free(mod, attrs,
				    sizeof (SMHBA_PORTATTRIBUTES));
				topo_mod_free(mod, sas_port,
				    sizeof (SMHBA_SAS_PORT));
				ret = -1;
				goto done;
			}

			/*
			 * Record that we created a unique port for this HBA.
			 * This will be referenced later if there are expanders
			 * in the topology.
			 */
			sas_hba_port = topo_mod_zalloc(mod,
			    sizeof (sas_port_t));
			sas_hba_port->sp_att_wwn = wwn_to_uint64(
			    sas_port->AttachedSASAddress);
			sas_hba_port->sp_vtx = hba_port;
			topo_list_append(hba_list, sas_hba_port);

			topo_node_setspecific(
			    topo_vertex_node(hba_port), sas_hba_port);

			/*
			 * Only create one logical initiator vertex for all
			 * of the HBA ports.
			 */
			/*
			 * XXX what to use for HBA phy info?
			 * phyinfo.start_phy = 0;
			 * phyinfo.end_phy = num_phys - 1;
			 */
			if (initiator == NULL) {
				if ((initiator = sas_create_vertex(mod,
				    TOPO_VTX_INITIATOR, hba_wwn, NULL))
				    == NULL) {
					topo_mod_free(mod, attrs,
					    sizeof (SMHBA_PORTATTRIBUTES));
					topo_mod_free(mod, sas_port,
					    sizeof (SMHBA_SAS_PORT));
					ret = -1;
					goto done;
				}
			}

			if (topo_edge_new(mod, initiator, hba_port) != 0) {
					topo_mod_free(mod, attrs,
					    sizeof (SMHBA_PORTATTRIBUTES));
					topo_mod_free(mod, sas_port,
					    sizeof (SMHBA_SAS_PORT));
					topo_vertex_destroy(mod, initiator);
					topo_vertex_destroy(mod, hba_port);
					ret = -1;
					goto done;
			}

			if (attrs->PortType == HBA_PORTTYPE_SASDEVICE) {
				/*
				 * Discovered a SAS or STP device connected
				 * directly to the HBA. This can sometimes
				 * include expander devices.
				 */
				topo_vertex_t *dev_port = NULL;
				topo_vertex_t *dev = NULL;

				if (sas_port->NumberofDiscoveredPorts > 1) {
					continue;
				}

				/*
				 * SMHBAAPI doesn't give us attached device phy
				 * information. For HBA_PORTTYPE_SASDEVICE only
				 * phy 0 will be in use, unless there are
				 * virtual phys.
				 */
				phyinfo.start_phy = 0;
				phyinfo.end_phy = 0;
				if ((dev_port = sas_create_vertex(mod,
				    TOPO_VTX_PORT,
				    wwn_to_uint64(sas_port->AttachedSASAddress),
				    &phyinfo))
				    == NULL) {
					topo_vertex_destroy(mod, initiator);
					topo_vertex_destroy(mod, hba_port);
					ret = -1;
					goto done;
				}
				if ((dev = sas_create_vertex(mod,
				    TOPO_VTX_TARGET,
				    wwn_to_uint64(sas_port->AttachedSASAddress),
				    &phyinfo))
				    == NULL) {
					topo_vertex_destroy(mod, initiator);
					topo_vertex_destroy(mod, hba_port);
					topo_vertex_destroy(mod, dev_port);
					ret = -1;
					goto done;
				}
				if (topo_edge_new(mod, hba_port, dev_port)
				    != 0) {
					topo_vertex_destroy(mod, initiator);
					topo_vertex_destroy(mod, hba_port);
					topo_vertex_destroy(mod, dev_port);
					topo_vertex_destroy(mod, dev);
					ret = -1;
					goto done;
				}
				if (topo_edge_new(mod, dev_port, dev)
				    != 0) {
					topo_vertex_destroy(mod, initiator);
					topo_vertex_destroy(mod, hba_port);
					topo_vertex_destroy(mod, dev_port);
					topo_vertex_destroy(mod, dev);
					ret = -1;
					goto done;
				}
			} else { /* Expanders? */
				continue;
			}
		}
	}

	/* Iterate through the expanders in /dev/smp. */
	/* XXX why does topo_mod_devinfo() return ENOENT? */
	root = di_init("/", DINFOCPYALL);
	if (root == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "nil dev hdl %s\n", strerror(errno));
	}

	for (smp = di_drv_first_node("smp", root);
	    smp != DI_NODE_NIL;
	    smp = di_drv_next_node(smp)) {
		char *full_smp_path;

		smp_path = di_devfs_path(smp);
		full_smp_path = topo_mod_zalloc(
		    mod, strlen(smp_path) + strlen("/devices"));
		(void) sprintf(full_smp_path, "/devices%s:smp", smp_path);

		if ((ret = sas_expander_discover(mod, full_smp_path,
		    expd_list)) != 0) {
			topo_mod_dprintf(mod, "expander discovery failed\n");
			return (ret);
		}
	}

	sas_topo_iter_t iter;
	iter.sas_mod = mod;
	iter.sas_expd_list = expd_list;
	(void) topo_vertex_iter(mod->tm_hdl,
	    topo_digraph_get(mod->tm_hdl, FM_FMRI_SCHEME_SAS),
	    sas_vtx_iter, &iter);

	topo_mod_dprintf(mod, "final pass\n");
	(void) topo_vertex_iter(mod->tm_hdl,
	    topo_digraph_get(mod->tm_hdl, FM_FMRI_SCHEME_SAS),
	    sas_vtx_final_pass, &iter);

	topo_mod_dprintf(mod, "done\n");

done:
	if (expd_list) {
		for (sas_port_t *expd_port = topo_list_next(expd_list);
		    expd_port != NULL;
		    expd_port = topo_list_next(expd_port)) {
			topo_mod_free(mod, expd_port, sizeof (sas_port_t));
		}
		topo_mod_free(mod, expd_list, sizeof (topo_list_t));
	}
	if (hba_list) {
		for (sas_port_t *hba_port = topo_list_next(hba_list);
		    hba_port != NULL;
		    hba_port = topo_list_next(hba_port)) {
			topo_mod_free(mod, hba_port, sizeof (sas_port_t));
		}
		topo_mod_free(mod, hba_list, sizeof (topo_list_t));
	}
	(void) HBA_FreeLibrary();
	return (ret);
}

static void
sas_release(topo_mod_t *mod, tnode_t *node)
{
	topo_method_unregister_all(mod, node);
}

/*
 * XXX still need to implement the two methods below
 */

/*
 * This is a prop method that returns the dev-scheme FMRI of the component.
 * This should be registered on the underlying nodes for initiator, expander
 * and target vertices.
 */
static int
sas_dev_fmri(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	if (version > TOPO_METH_FMRI_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	return (-1);
}

/*
 * This is a prop method that returns the hc-scheme FMRI of the corresponding
 * component in the hc-scheme topology.  This should be registered on the
 * underlying nodes for initiator and non-SMP target vertices.
 *
 * For initiators this would be the corresponding pciexfn node.
 * For disk/ssd targets, this would be thew corresponding disk node.  For SES
 * targets, this would be the corresonding ses-enclosure node.  SMP targets
 * are not represented in the hc-scheme topology.
 */
static int
sas_hc_fmri(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	if (version > TOPO_METH_FMRI_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	return (-1);
}

static ssize_t
fmri_bufsz(nvlist_t *nvl)
{
	nvlist_t **paths, *auth;
	uint_t nelem;
	char *type;
	ssize_t bufsz = 0;
	uint32_t start_phy = UINT32_MAX, end_phy = UINT32_MAX;

	if (nvlist_lookup_nvlist(nvl, FM_FMRI_AUTHORITY, &auth) != 0 ||
	    nvlist_lookup_string(auth, FM_FMRI_SAS_TYPE, &type) != 0)
		return (0);

	(void) nvlist_lookup_uint32(auth, FM_FMRI_SAS_START_PHY, &start_phy);
	(void) nvlist_lookup_uint32(auth, FM_FMRI_SAS_END_PHY, &end_phy);

	if (start_phy != UINT32_MAX && end_phy != UINT32_MAX) {
		bufsz += snprintf(NULL, 0, "sas://%s=%s:%s=%u:%s=%u",
		    FM_FMRI_SAS_TYPE, type, FM_FMRI_SAS_START_PHY, start_phy,
		    FM_FMRI_SAS_END_PHY, end_phy);
	} else {
		bufsz += snprintf(NULL, 0, "sas://%s=%s", FM_FMRI_SAS_TYPE,
		    type);
	}

	if (nvlist_lookup_nvlist_array(nvl, FM_FMRI_SAS_PATH, &paths,
	    &nelem) != 0) {
		return (0);
	}

	for (uint_t i = 0; i < nelem; i++) {
		char *sasname;
		uint64_t sasaddr;

		if (nvlist_lookup_string(paths[i], FM_FMRI_SAS_NAME,
		    &sasname) != 0 ||
		    nvlist_lookup_uint64(paths[i], FM_FMRI_SAS_ADDR,
		    &sasaddr) != 0) {
			return (0);
		}
		bufsz += snprintf(NULL, 0, "/%s=%" PRIx64 "", sasname,
		    sasaddr);
	}
	return (bufsz);
}

static int
sas_fmri_nvl2str(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	uint8_t scheme_vers;
	nvlist_t *outnvl;
	nvlist_t **paths, *auth;
	uint_t nelem;
	ssize_t bufsz, end = 0;
	char *buf, *type;
	uint32_t start_phy = UINT32_MAX, end_phy = UINT32_MAX;

	if (version > TOPO_METH_NVL2STR_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_uint8(in, FM_FMRI_SAS_VERSION, &scheme_vers) != 0 ||
	    scheme_vers != FM_SAS_SCHEME_VERSION) {
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}

	/*
	 * Get size of buffer needed to hold the string representation of the
	 * FMRI.
	 */
	if ((bufsz = fmri_bufsz(in)) == 0) {
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));
	}

	if ((buf = topo_mod_zalloc(mod, bufsz)) == NULL) {
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	/*
	 * We've already successfully done these nvlist lookups in fmri_bufsz()
	 * so we don't worry about checking retvals this time around.
	 */
	(void) nvlist_lookup_nvlist(in, FM_FMRI_AUTHORITY, &auth);
	(void) nvlist_lookup_string(auth, FM_FMRI_SAS_TYPE, &type);
	(void) nvlist_lookup_uint32(auth, FM_FMRI_SAS_START_PHY, &start_phy);
	(void) nvlist_lookup_uint32(auth, FM_FMRI_SAS_END_PHY, &end_phy);
	(void) nvlist_lookup_nvlist_array(in, FM_FMRI_SAS_PATH, &paths,
	    &nelem);
	if (start_phy != UINT32_MAX && end_phy != UINT32_MAX)
		end += sprintf(buf, "sas://%s=%s:%s=%u:%s=%u",
		    FM_FMRI_SAS_TYPE, type, FM_FMRI_SAS_START_PHY, start_phy,
		    FM_FMRI_SAS_END_PHY, end_phy);
	else
		end += sprintf(buf, "sas://%s=%s", FM_FMRI_SAS_TYPE, type);

	for (uint_t i = 0; i < nelem; i++) {
		char *sasname;
		uint64_t sasaddr;

		(void) nvlist_lookup_string(paths[i], FM_FMRI_SAS_NAME,
		    &sasname);
		(void) nvlist_lookup_uint64(paths[i], FM_FMRI_SAS_ADDR,
		    &sasaddr);
		end += sprintf(buf + end, "/%s=%" PRIx64 "", sasname,
		    sasaddr);
	}

	if (topo_mod_nvalloc(mod, &outnvl, NV_UNIQUE_NAME) != 0) {
		topo_mod_free(mod, buf, bufsz);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	if (nvlist_add_string(outnvl, "fmri-string", buf) != 0) {
		nvlist_free(outnvl);
		topo_mod_free(mod, buf, bufsz);
		return (topo_mod_seterrno(mod, EMOD_FMRI_NVL));
	}
	topo_mod_free(mod, buf, bufsz);
	*out = outnvl;

	return (0);
}

static int
sas_fmri_str2nvl(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	char *fmristr, *tmp = NULL, *lastpair;
	char *sasname, *auth_field, *path_start;
	nvlist_t *fmri = NULL, *auth = NULL, **sas_path = NULL;
	uint_t npairs = 0, i = 0, fmrilen, path_offset;

	if (version > TOPO_METH_STR2NVL_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_string(in, "fmri-string", &fmristr) != 0)
		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));

	if (strncmp(fmristr, "sas://", 6) != 0)
		return (topo_mod_seterrno(mod, EMOD_FMRI_MALFORM));

	if (topo_mod_nvalloc(mod, &fmri, NV_UNIQUE_NAME) != 0) {
		/* errno set */
		return (-1);
	}
	if (nvlist_add_string(fmri, FM_FMRI_SCHEME,
	    FM_FMRI_SCHEME_SAS) != 0 ||
	    nvlist_add_uint8(fmri, FM_FMRI_SAS_VERSION,
	    FM_SAS_SCHEME_VERSION) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}

	/*
	 * We need to make a copy of the fmri string because strtok will
	 * modify it.  We can't use topo_mod_strdup/strfree because
	 * topo_mod_strfree will end up leaking part of the string because
	 * of the NUL chars that strtok inserts - which will cause
	 * topo_mod_strfree to miscalculate the length of the string.  So we
	 * keep track of the length of the original string and use
	 * topo_mod_zalloc/topo_mod_free.
	 */
	fmrilen = strlen(fmristr);
	if ((tmp = topo_mod_zalloc(mod, fmrilen + 1)) == NULL) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	(void) strncpy(tmp, fmristr, fmrilen);

	/*
	 * Find the offset of the "/" after the authority portion of the FMRI.
	 */
	if ((path_start = strchr(tmp + 6, '/')) == NULL) {
		(void) topo_mod_seterrno(mod, EMOD_FMRI_MALFORM);
		topo_mod_free(mod, tmp, fmrilen + 1);
		goto err;
	}
	path_offset = path_start - tmp;

	/*
	 * Count the number of "=" chars after the "sas:///" portion of the
	 * FMRI to determine how big the sas-path array needs to be.
	 */
	(void) strtok_r(tmp + path_offset, "=", &lastpair);
	while (strtok_r(NULL, "=", &lastpair) != NULL)
		npairs++;

	if ((sas_path = topo_mod_zalloc(mod, npairs * sizeof (nvlist_t *))) ==
	    NULL) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}

	/*
	 * Build the auth nvlist
	 */
	if (topo_mod_nvalloc(mod, &auth, NV_UNIQUE_NAME) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}

	(void) strncpy(tmp, fmristr, fmrilen);
	auth_field = tmp + 6;

	sasname = fmristr + path_offset + 1;

	while (auth_field < (tmp + path_offset)) {
		char *end, *auth_val;
		uint32_t phy;

		if ((end = strchr(auth_field, '=')) == NULL) {
			(void) topo_mod_seterrno(mod, EMOD_FMRI_MALFORM);
			goto err;
		}
		*end = '\0';
		auth_val = end + 1;

		if ((end = strchr(auth_val, ':')) == NULL &&
		    (end = strchr(auth_val, '/')) == NULL) {
			(void) topo_mod_seterrno(mod, EMOD_FMRI_MALFORM);
			goto err;
		}
		*end = '\0';

		if (strcmp(auth_field, FM_FMRI_SAS_TYPE) == 0) {
			(void) nvlist_add_string(auth, auth_field,
			    auth_val);
		} else if (strcmp(auth_field, FM_FMRI_SAS_START_PHY) == 0 ||
		    strcmp(auth_field, FM_FMRI_SAS_END_PHY) == 0) {

			phy = atoi(auth_val);
			(void) nvlist_add_uint32(auth, auth_field, phy);
		}
		auth_field = end + 1;
	}
	(void) nvlist_add_nvlist(fmri, FM_FMRI_AUTHORITY, auth);

	while (i < npairs) {
		nvlist_t *pathcomp;
		uint64_t sasaddr;
		char *end, *addrstr, *estr;

		if (topo_mod_nvalloc(mod, &pathcomp, NV_UNIQUE_NAME) != 0) {
			(void) topo_mod_seterrno(mod, EMOD_NOMEM);
			goto err;
		}
		if ((end = strchr(sasname, '=')) == NULL) {
			(void) topo_mod_seterrno(mod, EMOD_FMRI_MALFORM);
			goto err;
		}
		*end = '\0';
		addrstr = end + 1;

		/*
		 * If this is the last pair, then addrstr will already be
		 * nul-terminated.
		 */
		if (i < (npairs - 1)) {
			if ((end = strchr(addrstr, '/')) == NULL) {
				(void) topo_mod_seterrno(mod,
				    EMOD_FMRI_MALFORM);
				goto err;
			}
			*end = '\0';
		}

		/*
		 * Convert addrstr to a uint64_t
		 */
		errno = 0;
		sasaddr = strtoull(addrstr, &estr, 16);
		if (errno != 0 || *estr != '\0') {
			(void) topo_mod_seterrno(mod, EMOD_FMRI_MALFORM);
			goto err;
		}

		/*
		 * Add both nvpairs to the nvlist and then add the nvlist to
		 * the sas-path nvlist array.
		 */
		if (nvlist_add_string(pathcomp, FM_FMRI_SAS_NAME, sasname) !=
		    0 ||
		    nvlist_add_uint64(pathcomp, FM_FMRI_SAS_ADDR, sasaddr) !=
		    0) {
			(void) topo_mod_seterrno(mod, EMOD_NOMEM);
			goto err;
		}
		sas_path[i++] = pathcomp;
		sasname = end + 1;
	}
	if (nvlist_add_nvlist_array(fmri, FM_FMRI_SAS_PATH, sas_path,
	    npairs) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	*out = fmri;

	topo_mod_free(mod, tmp, fmrilen + 1);
	return (0);
err:
	topo_mod_dprintf(mod, "%s failed: %s", __func__,
	    topo_strerror(topo_mod_errno(mod)));
	if (sas_path != NULL) {
		for (i = 0; i < npairs; i++)
			nvlist_free(sas_path[i]);

		topo_mod_free(mod, sas_path, npairs * sizeof (nvlist_t *));
	}
	nvlist_free(fmri);
	topo_mod_free(mod, tmp, fmrilen + 1);
	return (-1);
}

/*
 * This method creates a sas-SCHEME FMRI that represents a pathnode.  This is
 * not intended to be called directly, but rather be called via
 * topo_mod_sasfmri()
 */
static int
sas_fmri_create(topo_mod_t *mod, tnode_t *node, topo_version_t version,
    nvlist_t *in, nvlist_t **out)
{
	char *nodename;
	uint64_t nodeinst;
	nvlist_t *fmri = NULL, *args, *auth, *saspath[1], *pathcomp = NULL;

	if (version > TOPO_METH_STR2NVL_VERSION)
		return (topo_mod_seterrno(mod, EMOD_VER_NEW));

	if (nvlist_lookup_string(in, TOPO_METH_FMRI_ARG_NAME, &nodename) !=
	    0 ||
	    nvlist_lookup_uint64(in, TOPO_METH_FMRI_ARG_INST, &nodeinst) !=
	    0 ||
	    nvlist_lookup_nvlist(in, TOPO_METH_FMRI_ARG_NVL, &args) != 0 ||
	    nvlist_lookup_nvlist(args, TOPO_METH_FMRI_ARG_AUTH, &auth) != 0) {

		return (topo_mod_seterrno(mod, EMOD_METHOD_INVAL));
	}

	if (topo_mod_nvalloc(mod, &fmri, NV_UNIQUE_NAME) != 0) {
		/* errno set */
		return (-1);
	}

	if (nvlist_add_nvlist(fmri, FM_FMRI_AUTHORITY, auth) != 0 ||
	    nvlist_add_string(fmri, FM_FMRI_SCHEME, FM_FMRI_SCHEME_SAS) != 0 ||
	    nvlist_add_uint8(fmri, FM_FMRI_SAS_VERSION, FM_SAS_SCHEME_VERSION)
	    != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}

	if (topo_mod_nvalloc(mod, &pathcomp, NV_UNIQUE_NAME) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	if (nvlist_add_string(pathcomp, FM_FMRI_SAS_NAME, nodename) != 0 ||
	    nvlist_add_uint64(pathcomp, FM_FMRI_SAS_ADDR, nodeinst) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	saspath[0] = pathcomp;

	if (nvlist_add_nvlist_array(fmri, FM_FMRI_SAS_PATH, saspath, 1) != 0) {
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		goto err;
	}
	*out = fmri;

	return (0);
err:
	topo_mod_dprintf(mod, "%s failed: %s", __func__,
	    topo_strerror(topo_mod_errno(mod)));
	nvlist_free(pathcomp);
	nvlist_free(fmri);
	return (-1);
}
