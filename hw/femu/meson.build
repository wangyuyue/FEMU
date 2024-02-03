softmmu_ss.add(when: 'CONFIG_FEMU_PCI', if_true: files( 'dma.c', 'intr.c', 'nvme-util.c', 'nvme-admin.c', 'nvme-io.c', 'femu.c', 'nossd/nop.c', 'nand/nand.c', 'timing-model/timing.c', 'ocssd/oc12.c', 'ocssd/oc20.c', 'zns/zns.c', 'bbssd/bb.c', 'bbssd/ftl.c', 'lib/pqueue.c', 'lib/rte_ring.c', 'backend/dram.c'))
softmmu_ss.add(when: 'CONFIG_FEMU_PCI', if_true: files('kv_get.c', 'kv_range.c','test_function.c', 'isc_function.c'))

bighash_deps = []
local_include = include_directories('/home/yuyue/.local/include')
local_lib = '/home/yuyue/.local/lib'
folly_dep = cxx.find_library('folly', dirs: local_lib)
fmt_dep = cxx.find_library('fmt', dirs: local_lib)
iberty_dep = cxx.find_library('iberty', dirs: '/usr/lib')

bighash_deps+= [folly_dep, fmt_dep, iberty_dep]
bighash_deps+= [dependency('glog')]
bighash_deps+= [dependency('double-conversion')]

cache_files = ['flash-cache/bighash/Bucket.cpp', 'flash-cache/bighash/BucketStorage.cpp', 'flash-cache/common/Buffer.cpp', 'flash-cache/common/Hash.cpp', 'flash-cache/common/Types.cpp']
libbighash = static_library('libbighash', sources: files('isc_cc.cpp', cache_files), dependencies: bighash_deps, include_directories: local_include, cpp_args: ['-Wno-undef'])

bighash = declare_dependency(link_with : libbighash)

softmmu_ss.add(bighash)