#include "constmem.hpp"
#include "envvar.hpp"

namespace rocshmem {

__constant__ constmem_t constmem;

void init_constant_memory(void) {
  std::string envstr;
  constmem_t constmem_values;

  memset(&constmem_values, 0, sizeof(constmem_t));

  envstr = envvar::gda::alltoallv_wg_algo;

  if (envstr.empty() || envstr.find("GET") != std::string::npos) {
    constmem_values.alltoall_wg_algo = gda::ALLTOALLV_WG_ALGO_GET;
  } else {
    constmem_values.alltoall_wg_algo = gda::ALLTOALLV_WG_ALGO_COPY;
  }

  constmem_values.gda_put_wave_threshold     = envvar::gda::put_wave_threshold;
  constmem_values.gda_put_wave_nbi_threshold = envvar::gda::put_wave_nbi_threshold;

  CHECK_HIP(hipMemcpyToSymbol(HIP_SYMBOL(constmem), &constmem_values, sizeof(constmem_t)));
}

}
