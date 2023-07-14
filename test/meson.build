


# test_network = executable(
#     'test_network', 
#     sources: ['test_network.c', 'uca-phantom-communicate.c'],
#     dependencies: deps,
#     # link_with: phantom,
#     c_args: c_args,
#     include_directories: [headers, uca_headers]
# )

test_network = executable(
    'test_unit_Variables', 
    sources: ['test_unit_variables.c', '../uca-phantom-communicate.c', '../ringbuf.c'],
    dependencies: deps,
    # link_with: phantom,
    c_args: c_args,
    include_directories: [headers, uca_headers]
)

# test_network = executable(
#     'test_pfring_recv_ring', 
#     sources: ['test_pfring_recv_ring.c', '../uca-phantom-communicate.c', '../ringbuf.c'],
#     dependencies: deps,
#     # link_with: phantom,
#     c_args: c_args,
#     include_directories: [headers, uca_headers]
# )


# test_timestamps = executable(
#     'test_timestamps', 
#     sources: ['test_timestamps.c', 'uca-phantom-communicate.c'],
#     dependencies: deps,
#     # link_with: phantom,
#     c_args: c_args,
#     include_directories: [headers, uca_headers]
# )

# test_default = executable(
#     'test_default', 
#     sources: ['test_default.c', 'uca-phantom-camera-re.c'],
#     dependencies: deps
# )
