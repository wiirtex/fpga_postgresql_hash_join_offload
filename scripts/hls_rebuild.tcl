# hls_rebuild.tcl — re-run csim + synthesis + IP export for hash_join_kernel
#
# Usage (from repo root):
#   D:/HLS/2025.2/Vitis/bin/vitis-run --mode hls --tcl scripts/hls_rebuild.tcl

set repo_root [file normalize [file dirname [file dirname [info script]]]]
set ip_out_dir [file join $repo_root firstlich hls impl ip]
set hls_impl_dir [file join $repo_root firstlich hls impl]
set hls_syn_dir [file join $repo_root firstlich hls syn]
set hls_cflags ""
if {[info exists ::env(HLS_DISABLE_DEBUG)] && $::env(HLS_DISABLE_DEBUG) ne "0"} {
    set hls_cflags "-D__SYNTHESIS_DISABLE_DEBUG__"
    puts "HLS performance build: kernel debug frames are disabled"
}

open_project [file join $repo_root firstlich]
open_solution hls
set_part xc7a100tcsg324-1
set_top hash_join_kernel
set hls_clock_period 15
if {[info exists ::env(HLS_CLOCK_PERIOD_NS)] && $::env(HLS_CLOCK_PERIOD_NS) ne ""} {
    set hls_clock_period $::env(HLS_CLOCK_PERIOD_NS)
}
puts "HLS clock period target: ${hls_clock_period} ns"
create_clock -period $hls_clock_period

# Diagnostic builds must not silently pipeline short loops such as
# LinearHashTable's 16-step probe chain. The production architecture can revisit
# this once the board correctness issue is isolated.
config_compile -pipeline_loops 0

# Clear any stale -DHLS_COSIM_MODE flags that were accidentally saved to this solution.
# add_files on an already-known file updates its cflags in-place.
set src [file join $repo_root src]
add_files -cflags $hls_cflags [file join $src hash_join_kernel.cpp]
add_files -cflags $hls_cflags [file join $src hash_join_kernel.hpp]
add_files -cflags $hls_cflags [file join $src hash_join_types.hpp]
add_files -cflags $hls_cflags [file join $src hash_join_linear.hpp]
add_files -cflags $hls_cflags [file join $src hash_join_grace.hpp]
add_files -tb [file join $src hash_join_tb.cpp]

csim_design
csynth_design

# Vitis may leave stale HDL under impl/verilog, impl/vhdl, and impl/ip/hdl while
# still refreshing component.xml and export.zip.  Remove those generated outputs
# and seed impl/ from the freshly synthesized RTL so export_design cannot reuse
# an old RTL payload.
if {[file exists $ip_out_dir]} {
    file delete -force $ip_out_dir
}
foreach stale_dir [list \
    [file join $hls_impl_dir verilog] \
    [file join $hls_impl_dir vhdl] \
] {
    if {[file exists $stale_dir]} {
        file delete -force $stale_dir
    }
}
file mkdir $hls_impl_dir
foreach rtl_dir {verilog vhdl} {
    set src_rtl_dir [file join $hls_syn_dir $rtl_dir]
    set dst_rtl_dir [file join $hls_impl_dir $rtl_dir]
    if {![file exists $src_rtl_dir]} {
        error "HLS synthesis did not create $src_rtl_dir"
    }
    file copy -force $src_rtl_dir $dst_rtl_dir
}
export_design -format ip_catalog -output $ip_out_dir

# Sanity check: the current fix anchors outer_total through result_count_reg.
# If the packaged IP HDL does not contain this assignment, Vivado would build a
# bitstream from stale RTL and smoke tests would hang waiting for PHASE_DONE.
set packaged_rtl [file join $ip_out_dir hdl verilog hash_join_kernel.v]
if {![file exists $packaged_rtl]} {
    error "HLS export did not create $packaged_rtl"
}
set fh [open $packaged_rtl r]
set rtl_text [read $fh]
close $fh
if {![regexp {result_count_reg = grp_rx_u32_fu_[0-9]+_ap_return} $rtl_text]} {
    error "Packaged HLS RTL does not contain outer_total retention fix"
}

puts "\n=== HLS done. IP written to firstlich/hls/impl/ip/ ===\n"
