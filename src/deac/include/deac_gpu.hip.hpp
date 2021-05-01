/**
 * @file deac_gpu.hip.hpp
 * @author Nathan Nichols
 * @date 04.19.2021
 *
 * @brief GPU kernels using HIP.
 */

#include "common_gpu.hpp"
#ifdef DEAC_DEBUG
    #include <stdio.h>
#endif

#ifndef DEAC_GPU_HIP_H 
#define DEAC_GPU_HIP_H

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// GPU KERNELS ---------------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

#ifdef DEAC_DEBUG
    __global__
    void gpu_check_array(double * _array, int length) {
        int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
        if (i < length) {
            printf("_array[%d]: %e\n", i, _array[i]);
        }
    }
#endif

// GPU Kernel for reduction using warp (uses appropriate warp for NVIDIA vs AMD devices i. e. "portable wave aware code")
__device__ void warp_reduce(volatile double *sdata, unsigned int thread_idx) {
    if (warpSize == 64) { if (GPU_BLOCK_SIZE >= 128) sdata[thread_idx] += sdata[thread_idx + 64]; }
    if (GPU_BLOCK_SIZE >= 64) sdata[thread_idx] += sdata[thread_idx + 32];
    if (GPU_BLOCK_SIZE >= 32) sdata[thread_idx] += sdata[thread_idx + 16];
    if (GPU_BLOCK_SIZE >= 16) sdata[thread_idx] += sdata[thread_idx + 8];
    if (GPU_BLOCK_SIZE >= 8) sdata[thread_idx] += sdata[thread_idx + 4];
    if (GPU_BLOCK_SIZE >= 4) sdata[thread_idx] += sdata[thread_idx + 2];
    if (GPU_BLOCK_SIZE >= 2) sdata[thread_idx] += sdata[thread_idx + 1];
}

__device__ void warp_reduce_min(volatile double *sdata, unsigned int thread_idx) {
    if (warpSize == 64) { if (GPU_BLOCK_SIZE >= 128) sdata[thread_idx] = sdata[thread_idx + 64] < sdata[thread_idx] ? sdata[thread_idx + 64] : sdata[thread_idx]; }
    if (GPU_BLOCK_SIZE >= 64) sdata[thread_idx] = sdata[thread_idx + 32] < sdata[thread_idx] ? sdata[thread_idx + 32] : sdata[thread_idx];
    if (GPU_BLOCK_SIZE >= 32) sdata[thread_idx] = sdata[thread_idx + 16] < sdata[thread_idx] ? sdata[thread_idx + 16] : sdata[thread_idx];
    if (GPU_BLOCK_SIZE >= 16) sdata[thread_idx] = sdata[thread_idx + 8] < sdata[thread_idx] ? sdata[thread_idx + 8] : sdata[thread_idx];
    if (GPU_BLOCK_SIZE >= 8) sdata[thread_idx] = sdata[thread_idx + 4] < sdata[thread_idx] ? sdata[thread_idx + 4] : sdata[thread_idx];
    if (GPU_BLOCK_SIZE >= 4) sdata[thread_idx] = sdata[thread_idx + 2] < sdata[thread_idx] ? sdata[thread_idx + 2] : sdata[thread_idx];
    if (GPU_BLOCK_SIZE >= 2) sdata[thread_idx] = sdata[thread_idx + 1] < sdata[thread_idx] ? sdata[thread_idx + 1] : sdata[thread_idx];
}

__global__
void gpu_matrix_multiply_MxN_by_Nx1(double * C, double * A, double * B, int N, int idx) {
    __shared__ double _c[GPU_BLOCK_SIZE];
    int _j = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (_j < N) {
        _c[hipThreadIdx_x] = A[idx*N + _j]*B[_j];
    } else {
        _c[hipThreadIdx_x] = 0.0;
    }
    __syncthreads();

    // NEED TO REDUCE _c ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 512];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 256];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 128];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 64];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce(_c, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        //NOTE: May see some performance gain here if temporarily store results
        // to some global device variable and then launch a separate kernel to
        // again reduce those results i.e.
        // tmp_c[hipBlockIdx_x] = _c[0];
        // ^-- reduce on this, but this code may get too bloated
        atomicAdd(&C[idx], _c[0]);
    }
}

__global__
void gpu_matrix_multiply_LxM_by_MxN(double * C, double * A, double * B, int L, int M, int idx) {
    __shared__ double _c[GPU_BLOCK_SIZE];
    int k = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (k < M) {
        int _i = idx/L;
        int _j = idx - _i*L;
        _c[hipThreadIdx_x] = A[_j*M + k]*B[_i*M + k];
    } else {
        _c[hipThreadIdx_x] = 0.0;
    }
    __syncthreads();

    // NEED TO REDUCE _c ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 512];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 256];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 128];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                _c[hipThreadIdx_x] += _c[hipThreadIdx_x + 64];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce(_c, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        //NOTE: May see some performance gain here if temporarily store results
        // to some global device variable and then launch a separate kernel to
        // again reduce those results i.e.
        // tmp_c[hipBlockIdx_x] = _c[0];
        // ^-- reduce on this, but this code may get too bloated
        atomicAdd(&C[idx], _c[0]);
    }
}

__global__
void gpu_normalize_population(double * population, double * normalization, double zeroth_moment, int population_size, int genome_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size*genome_size) {
        double _norm = normalization[i/genome_size];
        population[i] *= zeroth_moment/_norm;
    }
}

__global__
void gpu_set_fitness(double * fitness, double * isf, double * isf_model, double * isf_error, int number_of_timeslices, int idx) {
    __shared__ double _f[GPU_BLOCK_SIZE];
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < number_of_timeslices) {
        _f[hipThreadIdx_x] = pow((isf[i] - isf_model[idx*number_of_timeslices + i])/isf_error[i],2);
    } else {
        _f[hipThreadIdx_x] = 0.0;
    }
    __syncthreads();

    // NEED TO REDUCE _f ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 512];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 256];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 128];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 64];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce(_f, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        //NOTE: May see some performance gain here if temporarily store results
        // to some global device variable and then launch a separate kernel to
        // again reduce those results i.e.
        // tmp_f[hipBlockIdx_x] = _f[0];
        // ^-- reduce on this, but this code may get too bloated
        atomicAdd(&fitness[idx], _f[0]/number_of_timeslices);
    }
}

__global__
void gpu_set_fitness_moments_reduced_chi_squared(double * fitness, double * moments, double moment, double moment_error, int population_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        fitness[i] += pow((moment - moments[i])/moment_error,2);
    } 
}

__global__
void gpu_set_fitness_moments_chi_squared(double * fitness, double * moments, double moment, int population_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        fitness[i] += pow((moment - moments[i]),2)/moment;
    } 
}

__global__
void gpu_get_minimum_fitness(double * fitness, double * minimum_fitness, int population_size) {
    __shared__ double s_minimum[GPU_BLOCK_SIZE];
    if (hipThreadIdx_x < population_size) {
        s_minimum[hipThreadIdx_x] = fitness[hipThreadIdx_x];
    } else {
        s_minimum[hipThreadIdx_x] = fitness[0];
    }

    for (int i=0; i<population_size/GPU_BLOCK_SIZE; i++) {
        int j = GPU_BLOCK_SIZE*i + hipThreadIdx_x;
        if (j < population_size) {
            s_minimum[hipThreadIdx_x] = fitness[j] < s_minimum[hipThreadIdx_x] ? fitness[j] : s_minimum[hipThreadIdx_x];
        }
    }

    __syncthreads();

    // NEED TO REDUCE _f ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            s_minimum[hipThreadIdx_x] = s_minimum[hipThreadIdx_x + 512] < s_minimum[hipThreadIdx_x] ? s_minimum[hipThreadIdx_x + 512] : s_minimum[hipThreadIdx_x];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            s_minimum[hipThreadIdx_x] = s_minimum[hipThreadIdx_x + 256] < s_minimum[hipThreadIdx_x] ? s_minimum[hipThreadIdx_x + 256] : s_minimum[hipThreadIdx_x];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            s_minimum[hipThreadIdx_x] = s_minimum[hipThreadIdx_x + 128] < s_minimum[hipThreadIdx_x] ? s_minimum[hipThreadIdx_x + 128] : s_minimum[hipThreadIdx_x];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                s_minimum[hipThreadIdx_x] = s_minimum[hipThreadIdx_x + 64] < s_minimum[hipThreadIdx_x] ? s_minimum[hipThreadIdx_x + 64] : s_minimum[hipThreadIdx_x];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce_min(s_minimum, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        *minimum_fitness = s_minimum[0];
    }

}

__global__
void gpu_set_fitness_mean(double * fitness_mean, double * fitness, int population_size, int idx) {
    __shared__ double _f[GPU_BLOCK_SIZE];
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        _f[hipThreadIdx_x] = fitness[i];
    } else {
        _f[hipThreadIdx_x] = 0.0;
    }
    __syncthreads();

    // NEED TO REDUCE _f ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 512];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 256];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 128];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 64];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce(_f, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        //NOTE: May see some performance gain here if temporarily store results
        // to some global device variable and then launch a separate kernel to
        // again reduce those results i.e.
        // tmp_f[hipBlockIdx_x] = _f[0];
        // ^-- reduce on this, but this code may get too bloated
        atomicAdd(&fitness_mean[idx], _f[0]/population_size);
    }
}

__global__
void gpu_set_fitness_standard_deviation(double * fitness_standard_deviation, double * fitness_mean, double * fitness, int population_size, int idx) {
    __shared__ double _f[GPU_BLOCK_SIZE];
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        _f[hipThreadIdx_x] = pow(fitness[i] - fitness_mean[idx],2);
    } else {
        _f[hipThreadIdx_x] = 0.0;
    }
    __syncthreads();

    // NEED TO REDUCE _f ON SHARED MEMORY AND ADD TO GLOBAL isf
    if (GPU_BLOCK_SIZE >= 1024) {
        if (hipThreadIdx_x < 512) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 512];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 512) {
        if (hipThreadIdx_x < 256) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 256];
        }
        __syncthreads();
    } 

    if (GPU_BLOCK_SIZE >= 256) {
        if (hipThreadIdx_x < 128) {
            _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 128];
        }
        __syncthreads();
    } 

    if (warpSize == 32) {
        if (GPU_BLOCK_SIZE >= 128) {
            if (hipThreadIdx_x < 64) {
                _f[hipThreadIdx_x] += _f[hipThreadIdx_x + 64];
            }
            __syncthreads();
        } 
    }

    if (hipThreadIdx_x < warpSize) {
        warp_reduce(_f, hipThreadIdx_x);
    }

    if (hipThreadIdx_x == 0) {
        //NOTE: May see some performance gain here if temporarily store results
        // to some global device variable and then launch a separate kernel to
        // again reduce those results i.e.
        // tmp_f[hipBlockIdx_x] = _f[0];
        // ^-- reduce on this, but this code may get too bloated
        atomicAdd(&fitness_mean[idx], _f[0]/population_size);
    }
}

__global__
void gpu_set_fitness_standard_deviation_sqrt(double * fitness_standard_deviation, int max_generations) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < max_generations) {
        fitness_standard_deviation[i] = sqrt(fitness_standard_deviation[i]);
    }
}

__global__
void gpu_set_population_new(double * population_new, double * population_old, int * mutant_indices, double * differential_weights_new, bool * mutate_indices, int population_size, int genome_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size*genome_size) {
        int _i = i/genome_size;
        int _j = i - _i*genome_size;
        double F = differential_weights_new[_i];
        int mutant_index1 = mutant_indices[3*_i];
        int mutant_index2 = mutant_indices[3*_i + 1];
        int mutant_index3 = mutant_indices[3*_i + 2];
        bool mutate = mutate_indices[i];
        if (mutate) {
            population_new[i] = fabs( 
                population_old[mutant_index1*genome_size + _j] + F*(
                        population_old[mutant_index2*genome_size + _j] -
                        population_old[mutant_index3*genome_size + _j]));
        } else {
            population_new[i] = population_old[i];
        }
    }
}

__global__
void gpu_set_rejection_indices(bool * rejection_indices, double * fitness_new, double * fitness_old, int population_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        bool accept = fitness_new[i] <= fitness_old[i];
        rejection_indices[i] = accept;
        if (accept) {
            fitness_old[i] = fitness_new[i];
        }
    }
}

__global__
void gpu_swap_control_parameters(double * control_parameter_old, double * control_parameter_new, bool * rejection_indices, int population_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size) {
        if (rejection_indices[i]) {
            control_parameter_old[i] = control_parameter_new[i];
        }
    }
}

__global__
void gpu_swap_populations(double * population_old, double * population_new, bool * rejection_indices, int population_size, int genome_size) {
    int i = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    if (i < population_size*genome_size) {
        int _i = i/genome_size;
        if (rejection_indices[_i]) {
            population_old[i] = population_new[i];
        }
    }
}

#endif
