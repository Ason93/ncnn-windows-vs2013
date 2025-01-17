// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "eltwise.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace ncnn {

DEFINE_LAYER_CREATOR(Eltwise)

Eltwise::Eltwise()
{
}

#if NCNN_STDIO
#if NCNN_STRING
int Eltwise::load_param(FILE* paramfp)
{
    int nscan = fscanf(paramfp, "%d %d", &op_type, &num_coeff);
    if (nscan != 2)
    {
        fprintf(stderr, "Eltwise load_param failed %d\n", nscan);
        return -1;
    }

    if (num_coeff > 0)
    {
        coeffs.create(num_coeff);
        if (coeffs.empty())
            return -100;
        float* coeffs_ptr = coeffs;
        for (int i=0; i<num_coeff; i++)
        {
            int nscan = fscanf(paramfp, "%f", &coeffs_ptr[i]);
            if (nscan != 1)
            {
                fprintf(stderr, "Eltwise load_param failed %d\n", nscan);
                return -1;
            }
        }
    }

    return 0;
}
#endif // NCNN_STRING
int Eltwise::load_param_bin(FILE* paramfp)
{
    fread(&op_type, sizeof(int), 1, paramfp);

    fread(&num_coeff, sizeof(int), 1, paramfp);

    if (num_coeff > 0)
    {
        coeffs.create(num_coeff);
        if (coeffs.empty())
            return -100;
        float* coeffs_ptr = coeffs;
        fread(coeffs_ptr, sizeof(float), num_coeff, paramfp);
    }

    return 0;
}
#endif // NCNN_STDIO

int Eltwise::load_param(const unsigned char*& mem)
{
    op_type = *(int*)(mem);
    mem += 4;

    num_coeff = *(int*)(mem);
    mem += 4;

    coeffs = Mat(num_coeff, (float*)mem);
    mem += num_coeff * sizeof(float);

    return 0;
}

int Eltwise::forward(const std::vector<Mat>& bottom_blobs, std::vector<Mat>& top_blobs) const
{
    const Mat& bottom_blob = bottom_blobs[0];
    int w = bottom_blob.w;
    int h = bottom_blob.h;
    int channels = bottom_blob.c;
    int size = w * h;

    Mat& top_blob = top_blobs[0];
    top_blob.create(w, h, channels);
    if (top_blob.empty())
        return -100;

    if (op_type == Operation_PROD)
    {
        // first blob
        const Mat& bottom_blob1 = bottom_blobs[1];
        #pragma omp parallel for
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_blob.channel(q);
            const float* ptr1 = bottom_blob1.channel(q);
            float* outptr = top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                outptr[i] = ptr[i] * ptr1[i];
            }
        }

        for (size_t b=2; b<bottom_blobs.size(); b++)
        {
            const Mat& bottom_blob1 = bottom_blobs[b];
            #pragma omp parallel for
            for (int q=0; q<channels; q++)
            {
                const float* ptr = bottom_blob1.channel(q);
                float* outptr = top_blob.channel(q);

                for (int i=0; i<size; i++)
                {
                    outptr[i] *= ptr[i];
                }
            }
        }
    }
    else if (op_type == Operation_SUM)
    {
        if (num_coeff == 0)
        {
            // first blob
            const Mat& bottom_blob1 = bottom_blobs[1];
            #pragma omp parallel for
            for (int q=0; q<channels; q++)
            {
                const float* ptr = bottom_blob.channel(q);
                const float* ptr1 = bottom_blob1.channel(q);
                float* outptr = top_blob.channel(q);

                for (int i=0; i<size; i++)
                {
                    outptr[i] = ptr[i] + ptr1[i];
                }
            }

            for (size_t b=2; b<bottom_blobs.size(); b++)
            {
                const Mat& bottom_blob1 = bottom_blobs[b];
                #pragma omp parallel for
                for (int q=0; q<channels; q++)
                {
                    const float* ptr = bottom_blob1.channel(q);
                    float* outptr = top_blob.channel(q);

                    for (int i=0; i<size; i++)
                    {
                        outptr[i] += ptr[i];
                    }
                }
            }
        }
        else
        {
            const float* coeffs_ptr = coeffs;

            // first blob
            const Mat& bottom_blob1 = bottom_blobs[1];
            float coeff0 = coeffs_ptr[0];
            float coeff1 = coeffs_ptr[1];
            #pragma omp parallel for
            for (int q=0; q<channels; q++)
            {
                const float* ptr = bottom_blob.channel(q);
                const float* ptr1 = bottom_blob1.channel(q);
                float* outptr = top_blob.channel(q);

                for (int i=0; i<size; i++)
                {
                    outptr[i] = ptr[i] * coeff0 + ptr1[i] * coeff1;
                }
            }

            for (size_t b=2; b<bottom_blobs.size(); b++)
            {
                const Mat& bottom_blob1 = bottom_blobs[b];
                float coeff = coeffs_ptr[b];
                #pragma omp parallel for
                for (int q=0; q<channels; q++)
                {
                    const float* ptr = bottom_blob1.channel(q);
                    float* outptr = top_blob.channel(q);

                    for (int i=0; i<size; i++)
                    {
                        outptr[i] += ptr[i] * coeff;
                    }
                }
            }
        }
    }
    else if (op_type == Operation_MAX)
    {
        // first blob
        const Mat& bottom_blob1 = bottom_blobs[1];
        #pragma omp parallel for
        for (int q=0; q<channels; q++)
        {
            const float* ptr = bottom_blob.channel(q);
            const float* ptr1 = bottom_blob1.channel(q);
            float* outptr = top_blob.channel(q);

            for (int i=0; i<size; i++)
            {
                outptr[i] = max(ptr[i], ptr1[i]);
            }
        }

        for (size_t b=2; b<bottom_blobs.size(); b++)
        {
            const Mat& bottom_blob1 = bottom_blobs[b];
            #pragma omp parallel for
            for (int q=0; q<channels; q++)
            {
                const float* ptr = bottom_blob1.channel(q);
                float* outptr = top_blob.channel(q);

                for (int i=0; i<size; i++)
                {
                    outptr[i] = max(outptr[i], ptr[i]);
                }
            }
        }
    }

    return 0;
}

} // namespace ncnn
