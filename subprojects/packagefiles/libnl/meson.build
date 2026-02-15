project('libnl', 'c', version: '3.8.0', license: 'LGPL-2.1-only')

cc = meson.get_compiler('c')

# Generate config.h
conf = configuration_data()
conf.set('HAVE_DLFCN_H', cc.has_header('dlfcn.h'))
conf.set('HAVE_INTTYPES_H', cc.has_header('inttypes.h'))
conf.set('HAVE_STDINT_H', cc.has_header('stdint.h'))
conf.set('HAVE_STDIO_H', cc.has_header('stdio.h'))
conf.set('HAVE_STDLIB_H', cc.has_header('stdlib.h'))
conf.set('HAVE_STRING_H', cc.has_header('string.h'))
conf.set('HAVE_STRINGS_H', cc.has_header('strings.h'))
conf.set('HAVE_SYS_STAT_H', cc.has_header('sys/stat.h'))
conf.set('HAVE_SYS_TYPES_H', cc.has_header('sys/types.h'))
conf.set('HAVE_UNISTD_H', cc.has_header('unistd.h'))
conf.set('HAVE_STRERROR_L', cc.has_function('strerror_l'))
conf.set10('HAVE_DECL_GETPROTOBYNAME_R', cc.has_function('getprotobyname_r'))
conf.set10('HAVE_DECL_GETPROTOBYNUMBER_R', cc.has_function('getprotobynumber_r'))
conf.set_quoted('PACKAGE', 'libnl')
conf.set_quoted('PACKAGE_NAME', 'libnl')
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', 'libnl ' + meson.project_version())
conf.set_quoted('PACKAGE_TARNAME', 'libnl')
conf.set_quoted('PACKAGE_URL', '')
conf.set_quoted('PACKAGE_BUGREPORT', '')
conf.set_quoted('VERSION', meson.project_version())
conf.set('STDC_HEADERS', true)

pthread_dep = dependency('threads')
conf.set('HAVE_LIBPTHREAD', true)

config_h = configure_file(output: 'config.h', configuration: conf)

# Include directories
libnl_inc = include_directories(
  'include',
  'include/linux-private',
)
# The generated config.h goes into the build directory
libnl_build_inc = include_directories('.')

# ---- libnl-3 (core) ----
libnl_core_sources = files(
  'lib/addr.c',
  'lib/attr.c',
  'lib/cache.c',
  'lib/cache_mngr.c',
  'lib/cache_mngt.c',
  'lib/data.c',
  'lib/error.c',
  'lib/handlers.c',
  'lib/hash.c',
  'lib/hashtable.c',
  'lib/mpls.c',
  'lib/msg.c',
  'lib/nl.c',
  'lib/object.c',
  'lib/socket.c',
  'lib/utils.c',
  'lib/version.c',
)

libnl_core_lib = static_library(
  'nl-3',
  libnl_core_sources,
  include_directories: [libnl_inc, libnl_build_inc],
  dependencies: pthread_dep,
  c_args: ['-D_GNU_SOURCE'],
)

libnl_dep = declare_dependency(
  include_directories: libnl_inc,
  link_with: libnl_core_lib,
  dependencies: pthread_dep,
)

# ---- libnl-route-3 ----
libnl_route_sources = files(
  'lib/route/act.c',
  'lib/route/addr.c',
  'lib/route/class.c',
  'lib/route/classid.c',
  'lib/route/cls.c',
  'lib/route/link.c',
  'lib/route/mdb.c',
  'lib/route/neigh.c',
  'lib/route/neightbl.c',
  'lib/route/netconf.c',
  'lib/route/nexthop.c',
  'lib/route/nexthop_encap.c',
  'lib/route/nh.c',
  'lib/route/nh_encap_mpls.c',
  'lib/route/pktloc.c',
  'lib/route/qdisc.c',
  'lib/route/route.c',
  'lib/route/route_obj.c',
  'lib/route/route_utils.c',
  'lib/route/rtnl.c',
  'lib/route/rule.c',
  'lib/route/tc.c',
  # link modules
  'lib/route/link/api.c',
  'lib/route/link/bonding.c',
  'lib/route/link/bridge.c',
  'lib/route/link/bridge_info.c',
  'lib/route/link/can.c',
  'lib/route/link/dummy.c',
  'lib/route/link/geneve.c',
  'lib/route/link/ifb.c',
  'lib/route/link/inet.c',
  'lib/route/link/inet6.c',
  'lib/route/link/ip6gre.c',
  'lib/route/link/ip6tnl.c',
  'lib/route/link/ip6vti.c',
  'lib/route/link/ipgre.c',
  'lib/route/link/ipip.c',
  'lib/route/link/ipvlan.c',
  'lib/route/link/ipvti.c',
  'lib/route/link/macsec.c',
  'lib/route/link/macvlan.c',
  'lib/route/link/ppp.c',
  'lib/route/link/sit.c',
  'lib/route/link/sriov.c',
  'lib/route/link/team.c',
  'lib/route/link/veth.c',
  'lib/route/link/vlan.c',
  'lib/route/link/vrf.c',
  'lib/route/link/vxlan.c',
  'lib/route/link/xfrmi.c',
  # cls modules
  'lib/route/cls/basic.c',
  'lib/route/cls/cgroup.c',
  'lib/route/cls/ematch.c',
  'lib/route/cls/flower.c',
  'lib/route/cls/fw.c',
  'lib/route/cls/mall.c',
  'lib/route/cls/police.c',
  'lib/route/cls/u32.c',
  'lib/route/cls/ematch/cmp.c',
  'lib/route/cls/ematch/container.c',
  'lib/route/cls/ematch/meta.c',
  'lib/route/cls/ematch/nbyte.c',
  'lib/route/cls/ematch/text.c',
  # qdisc modules
  'lib/route/qdisc/blackhole.c',
  'lib/route/qdisc/cbq.c',
  'lib/route/qdisc/dsmark.c',
  'lib/route/qdisc/fifo.c',
  'lib/route/qdisc/fq_codel.c',
  'lib/route/qdisc/hfsc.c',
  'lib/route/qdisc/htb.c',
  'lib/route/qdisc/ingress.c',
  'lib/route/qdisc/mqprio.c',
  'lib/route/qdisc/netem.c',
  'lib/route/qdisc/plug.c',
  'lib/route/qdisc/prio.c',
  'lib/route/qdisc/red.c',
  'lib/route/qdisc/sfq.c',
  'lib/route/qdisc/tbf.c',
  # act modules
  'lib/route/act/gact.c',
  'lib/route/act/mirred.c',
  'lib/route/act/nat.c',
  'lib/route/act/skbedit.c',
  'lib/route/act/vlan.c',
)

libnl_route_lib = static_library(
  'nl-route-3',
  libnl_route_sources,
  include_directories: [libnl_inc, libnl_build_inc],
  link_with: libnl_core_lib,
  dependencies: pthread_dep,
  c_args: ['-D_GNU_SOURCE', '-DSYSCONFDIR="/etc"'],
)

libnl_route_dep = declare_dependency(
  include_directories: libnl_inc,
  link_with: [libnl_route_lib, libnl_core_lib],
  dependencies: pthread_dep,
)
