softmmu_ss.add(when: 'CONFIG_FEMU_PCI', if_true: files( 'dma.c', 'intr.c', 'nvme-util.c', 'nvme-admin.c', 'nvme-io.c', 'femu.c', 'nossd/nop.c', 'nand/nand.c', 'timing-model/timing.c', 'ocssd/oc12.c', 'ocssd/oc20.c', 'zns/zns.c', 'bbssd/bb.c', 'bbssd/ftl.c', 'lib/pqueue.c', 'lib/rte_ring.c', 'backend/dram.c'))
softmmu_ss.add(when: 'CONFIG_FEMU_PCI', if_true: files('kv_get.c', 'kv_range.c','test_function.c', 'isc_function.c'))

bighash_deps = []
folly_dep = cxx.find_library('folly', dirs: '/usr/lib')
fmt_dep = cxx.find_library('fmt', dirs: '/usr/lib')
iberty_dep = cxx.find_library('iberty', dirs: '/usr/lib')

bighash_deps+= [folly_dep, fmt_dep, iberty_dep]
bighash_deps+= [dependency('glog')]
bighash_deps+= [dependency('double-conversion')]

libbighash = static_library('libbighash', sources: 'isc_cc.cpp', dependencies: bighash_deps)

bighash = declare_dependency(link_with : libbighash)

softmmu_ss.add(bighash)