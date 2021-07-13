/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include <miopen/activ.hpp>
#include <miopen/kernel_cache.hpp>
#include <miopen/mlo_internal.hpp>
#include <miopen/float_equal.hpp>
#include <miopen/visit_float.hpp>
#include <miopen/activ/invoke_params.hpp>
#include <miopen/activ/problem_description.hpp>
#include <miopen/activ/solvers.hpp>
#include <miopen/find_solution.hpp>

namespace miopen {

miopenStatus_t ActivationDescriptor::Forward(Handle& handle,
                                             const void* alpha,
                                             const TensorDescriptor& xDesc,
                                             ConstData_t x,
                                             const void* beta,
                                             const TensorDescriptor& yDesc,
                                             Data_t y,
                                             size_t xOffset,
                                             size_t yOffset)
{
    if(!float_equal(*(static_cast<const float*>(alpha)), 1.0) ||
       !float_equal(*(static_cast<const float*>(beta)), 0))
    {
        MIOPEN_THROW("Only alpha=1 and beta=0 is supported");
    }

    const auto problem = activ::ProblemDescription{*this, xDesc, yDesc};

    const auto invoke_params = [&]() {
        auto tmp     = activ::InvokeParams{};
        tmp.type     = InvokeType::Run;
        tmp.alpha    = GetAlpha();
        tmp.beta     = GetBeta();
        tmp.gamma    = GetGamma();
        tmp.x        = x;
        tmp.x_desc   = xDesc;
        tmp.y        = y;
        tmp.y_desc   = yDesc;
        tmp.x_offset = xOffset;
        tmp.y_offset = yOffset;
        return tmp;
    }();

    const auto algo           = AlgorithmName{"miopenActivationForward"};
    const auto network_config = problem.MakeNetworkConfig();

    if(const auto invoker = handle.GetInvoker(network_config, boost::none, algo))
    {
        (*invoker)(handle, invoke_params);
        return miopenStatusSuccess;
    }

    const auto ctx = ExecutionContext{&handle};
    const auto solvers =
        solver::SolverContainer<solver::activ::ActivFwdSolver0, solver::activ::ActivFwdSolver1>{};
    const auto slns = solvers.SearchForSolutions(ctx, problem, 1);

    if(slns.empty())
        MIOPEN_THROW(miopenStatusNotImplemented, "No solver found for activation forward.");

    const auto& sln = slns.front();
    if(!sln.invoker_factory)
        MIOPEN_THROW(miopenStatusInternalError, "Invoker missing in solver " + sln.solver_id);
    const auto invoker = handle.PrepareInvoker(*sln.invoker_factory, sln.construction_params);
    handle.RegisterInvoker(invoker, network_config, sln.solver_id, algo);
    invoker(handle, invoke_params);
    return miopenStatusSuccess;
}

miopenStatus_t ActivationDescriptor::Backward(Handle& handle,
                                              const void* alpha,
                                              const TensorDescriptor& yDesc,
                                              ConstData_t y,
                                              const TensorDescriptor& dyDesc,
                                              ConstData_t dy,
                                              const TensorDescriptor& xDesc,
                                              ConstData_t x,
                                              const void* beta,
                                              const TensorDescriptor& dxDesc,
                                              Data_t dx,
                                              size_t yOffset,
                                              size_t dyOffset,
                                              size_t xOffset,
                                              size_t dxOffset)
{
    if(!float_equal(*(static_cast<const float*>(alpha)), 1.0) ||
       !float_equal(*(static_cast<const float*>(beta)), 0))
    {
        MIOPEN_THROW("Only alpha=1 and beta=0 is supported");
    }

    const auto problem = activ::ProblemDescription{*this, xDesc, yDesc, dxDesc, dyDesc};

    const auto invoke_params = [&]() {
        auto tmp      = activ::BwdInvokeParams{};
        tmp.type      = InvokeType::Run;
        tmp.alpha     = GetAlpha();
        tmp.beta      = GetBeta();
        tmp.gamma     = GetGamma();
        tmp.x         = x;
        tmp.x_desc    = xDesc;
        tmp.y         = y;
        tmp.y_desc    = yDesc;
        tmp.dx        = dx;
        tmp.dx_desc   = dxDesc;
        tmp.dy        = dy;
        tmp.dy_desc   = dyDesc;
        tmp.x_offset  = xOffset;
        tmp.y_offset  = yOffset;
        tmp.dx_offset = dxOffset;
        tmp.dy_offset = dyOffset;
        return tmp;
    }();

    const auto algo           = AlgorithmName{"miopenActivationBackward"};
    const auto network_config = problem.MakeNetworkConfig();

    if(const auto invoker = handle.GetInvoker(network_config, boost::none, algo))
    {
        (*invoker)(handle, invoke_params);
        return miopenStatusSuccess;
    }

    const auto ctx     = ExecutionContext{&handle};
    const auto solvers = solver::SolverContainer<solver::activ::ActivBwdSolver0>{};
    const auto slns    = solvers.SearchForSolutions(ctx, problem, 1);

    // Copy from forward, uncomment when whole direction converted.
    // if(slns.empty())
    //    MIOPEN_THROW(miopenStatusNotImplemented, "No solver found for activation forward.");

    if(!slns.empty())
    {
        const auto& sln = slns.front();
        if(!sln.invoker_factory)
            MIOPEN_THROW(miopenStatusInternalError, "Invoker missing in solver " + sln.solver_id);
        const auto invoker = handle.PrepareInvoker(*sln.invoker_factory, sln.construction_params);
        handle.RegisterInvoker(invoker, network_config, sln.solver_id, algo);
        invoker(handle, invoke_params);
        return miopenStatusSuccess;
    }

    miopenStatus_t status = miopenStatusSuccess;

    mlo_construct_neuron construct_params(conv::Direction::BackwardData);

    double activ_alpha = GetAlpha();
    double activ_beta  = GetBeta();
    double activ_gamma = GetGamma();

    visit_float(xDesc.GetType(), [&](auto as_float) {

        construct_params.setStream(&handle);
        int ndOut       = 1;
        int cdOut       = 1;
        int hdOut       = 1;
        int wdOut       = 1;
        int ndOutStride = 0;
        int cdOutStride = 0;
        int hdOutStride = 0;
        int wdOutStride = 0;

        if(dyDesc.GetSize() == 4)
        {
            std::tie(ndOut, cdOut, hdOut, wdOut) = tien<4>(dyDesc.GetLengths());
            std::tie(ndOutStride, cdOutStride, hdOutStride, wdOutStride) =
                tien<4>(dyDesc.GetStrides());
        }
        else if(dyDesc.GetSize() < 4 && dyDesc.GetSize() > 0)
        {
            auto tensor_size = dyDesc.GetSize();
            switch(tensor_size)
            {
            case 1:
                std::tie(wdOut)       = tien<1>(dyDesc.GetLengths());
                std::tie(wdOutStride) = tien<1>(dyDesc.GetStrides());
                ndOutStride           = wdOut * wdOutStride;
                cdOutStride           = wdOut * wdOutStride;
                hdOutStride           = wdOut * wdOutStride;
                break;
            case 2:
                std::tie(hdOut, wdOut)             = tien<2>(dyDesc.GetLengths());
                std::tie(hdOutStride, wdOutStride) = tien<2>(dyDesc.GetStrides());
                ndOutStride = hdOut * hdOutStride;
                cdOutStride = hdOut * hdOutStride;
                break;
            case 3:
                std::tie(cdOut, hdOut, wdOut)                   = tien<3>(dyDesc.GetLengths());
                std::tie(cdOutStride, hdOutStride, wdOutStride) = tien<3>(dyDesc.GetStrides());
                ndOutStride = cdOut * cdOutStride;
                break;
            default: assert(false);
            }
        }
        else
        {
            MIOPEN_THROW("activation does not support tensor size larger than 3 or smaller than 1");
        }

        construct_params.setTopDfDescFromMLDesc(dyDesc);

        int nOut       = 1;
        int cOut       = 1;
        int hOut       = 1;
        int wOut       = 1;
        int nOutStride = 0;
        int cOutStride = 0;
        int hOutStride = 0;
        int wOutStride = 0;

        if(yDesc.GetSize() == 4)
        {
            std::tie(nOut, cOut, hOut, wOut)                         = tien<4>(yDesc.GetLengths());
            std::tie(nOutStride, cOutStride, hOutStride, wOutStride) = tien<4>(yDesc.GetStrides());
        }
        else if(yDesc.GetSize() < 4 && yDesc.GetSize() > 0)
        {
            auto tensor_size = yDesc.GetSize();
            switch(tensor_size)
            {
            case 1:
                std::tie(wOut)       = tien<1>(yDesc.GetLengths());
                std::tie(wOutStride) = tien<1>(yDesc.GetStrides());
                nOutStride           = wOut * wOutStride;
                cOutStride           = wOut * wOutStride;
                hOutStride           = wOut * wOutStride;
                break;
            case 2:
                std::tie(hOut, wOut)             = tien<2>(yDesc.GetLengths());
                std::tie(hOutStride, wOutStride) = tien<2>(yDesc.GetStrides());
                nOutStride = hOut * hOutStride;
                cOutStride = hOut * hOutStride;
                break;
            case 3:
                std::tie(cOut, hOut, wOut)                   = tien<3>(yDesc.GetLengths());
                std::tie(cOutStride, hOutStride, wOutStride) = tien<3>(yDesc.GetStrides());
                nOutStride = cOut * cOutStride;
                break;
            default: assert(false);
            }
        }
        else
        {
            MIOPEN_THROW("Activation does not support tensor dimensions larger than 3 or "
                         "smaller than 1");
        }

        construct_params.setTopDescFromMLDesc(yDesc);

        int ndIn       = 1;
        int cdIn       = 1;
        int hdIn       = 1;
        int wdIn       = 1;
        int ndInStride = 0;
        int cdInStride = 0;
        int hdInStride = 0;
        int wdInStride = 0;

        if(dxDesc.GetSize() == 4)
        {
            std::tie(ndIn, cdIn, hdIn, wdIn)                         = tien<4>(dxDesc.GetLengths());
            std::tie(ndInStride, cdInStride, hdInStride, wdInStride) = tien<4>(dxDesc.GetStrides());
        }
        else if(dxDesc.GetSize() < 4 && dxDesc.GetSize() > 0)
        {
            auto tensor_size = dxDesc.GetSize();
            switch(tensor_size)
            {
            case 1:
                std::tie(wdIn)       = tien<1>(dxDesc.GetLengths());
                std::tie(wdInStride) = tien<1>(dxDesc.GetStrides());
                ndInStride           = wdIn * wdInStride;
                cdInStride           = wdIn * wdInStride;
                hdInStride           = wdIn * wdInStride;
                break;
            case 2:
                std::tie(hdIn, wdIn)             = tien<2>(dxDesc.GetLengths());
                std::tie(hdInStride, wdInStride) = tien<2>(dxDesc.GetStrides());
                ndInStride = hdIn * hdInStride;
                cdInStride = hdIn * hdInStride;
                break;
            case 3:
                std::tie(cdIn, hdIn, wdIn)                   = tien<3>(dxDesc.GetLengths());
                std::tie(cdInStride, hdInStride, wdInStride) = tien<3>(dxDesc.GetStrides());
                ndInStride = cdIn * cdInStride;
                break;
            default: assert(false);
            }
        }
        else
        {
            MIOPEN_THROW("Activation does not support tensor dimensions larger than 3 or "
                         "smaller than 1");
        }

        construct_params.setBotDfDescFromMLDesc(dxDesc);

        int nIn       = 1;
        int cIn       = 1;
        int hIn       = 1;
        int wIn       = 1;
        int nInStride = 0;
        int cInStride = 0;
        int hInStride = 0;
        int wInStride = 0;

        if(xDesc.GetSize() == 4)
        {
            std::tie(nIn, cIn, hIn, wIn)                         = tien<4>(xDesc.GetLengths());
            std::tie(nInStride, cInStride, hInStride, wInStride) = tien<4>(xDesc.GetStrides());
        }
        else if(xDesc.GetSize() < 4 && xDesc.GetSize() > 0)
        {
            auto tensor_size = xDesc.GetSize();
            switch(tensor_size)
            {
            case 1:
                std::tie(wIn)       = tien<1>(xDesc.GetLengths());
                std::tie(wInStride) = tien<1>(xDesc.GetStrides());
                nInStride           = wIn * wInStride;
                cInStride           = wIn * wInStride;
                hInStride           = wIn * wInStride;
                break;
            case 2:
                std::tie(hIn, wIn)             = tien<2>(xDesc.GetLengths());
                std::tie(hInStride, wInStride) = tien<2>(xDesc.GetStrides());
                nInStride = hIn * hInStride;
                cInStride = hIn * hInStride;
                break;
            case 3:
                std::tie(cIn, hIn, wIn)                   = tien<3>(xDesc.GetLengths());
                std::tie(cInStride, hInStride, wInStride) = tien<3>(xDesc.GetStrides());
                nInStride = cIn * cInStride;
                break;
            default: assert(false);
            }
        }
        else
        {
            MIOPEN_THROW("Activation does not support tensor dimensions larger than 3 or "
                         "smaller than 1");
        }

        construct_params.setBotDescFromMLDesc(xDesc);

        int activ_mode = this->GetMode();

        construct_params.setNeuronDescr(activ_mode, activ_gamma, activ_beta, activ_alpha);

        mloConstruct(construct_params);

        std::string program_name     = construct_params.getKernelFile();      // CL kernel filename
        std::string kernel_name      = construct_params.getKernelName();      // kernel name
        std::string compiler_options = construct_params.getCompilerOptions(); // kernel parameters

        const std::vector<size_t>& vld = construct_params.getLocalWkSize();
        const std::vector<size_t>& vgd = construct_params.getGlobalWkSize();

        auto f_activ_alpha = as_float(this->GetAlpha());
        auto f_activ_beta  = as_float(this->GetBeta());
        auto f_activ_gamma = as_float(this->GetGamma());
        auto f_diff_scale  = f_activ_beta * f_activ_gamma;

        compiler_options +=
            " -DMIOPEN_N_IN=" + std::to_string(nIn) + " -DMIOPEN_C_IN=" + std::to_string(cIn) +
            " -DMIOPEN_H_IN=" + std::to_string(hIn) + " -DMIOPEN_W_IN=" + std::to_string(wIn) +
            " -DMIOPEN_N_IN_STRIDE=" + std::to_string(nInStride) + " -DMIOPEN_C_IN_STRIDE=" +
            std::to_string(cInStride) + " -DMIOPEN_H_IN_STRIDE=" + std::to_string(hInStride) +
            " -DMIOPEN_W_IN_STRIDE=" + std::to_string(wInStride) + " -DMIOPEN_N_OUT=" +
            std::to_string(nOut) + " -DMIOPEN_C_OUT=" + std::to_string(cOut) + " -DMIOPEN_H_OUT=" +
            std::to_string(hOut) + " -DMIOPEN_W_OUT=" + std::to_string(wOut) +
            " -DMIOPEN_N_OUT_STRIDE=" + std::to_string(nOutStride) + " -DMIOPEN_C_OUT_STRIDE=" +
            std::to_string(cOutStride) + " -DMIOPEN_H_OUT_STRIDE=" + std::to_string(hOutStride) +
            " -DMIOPEN_W_OUT_STRIDE=" + std::to_string(wOutStride) + " -DMIOPEN_N_DIN=" +
            std::to_string(ndIn) + " -DMIOPEN_C_DIN=" + std::to_string(cdIn) + " -DMIOPEN_H_DIN=" +
            std::to_string(hdIn) + " -DMIOPEN_W_DIN=" + std::to_string(wdIn) +
            " -DMIOPEN_N_DIN_STRIDE=" + std::to_string(ndInStride) + " -DMIOPEN_C_DIN_STRIDE=" +
            std::to_string(cdInStride) + " -DMIOPEN_H_DIN_STRIDE=" + std::to_string(hdInStride) +
            " -DMIOPEN_W_DIN_STRIDE=" + std::to_string(wdInStride) + " -DMIOPEN_N_DOUT=" +
            std::to_string(ndOut) + " -DMIOPEN_C_DOUT=" + std::to_string(cdOut) +
            " -DMIOPEN_H_DOUT=" + std::to_string(hdOut) + " -DMIOPEN_W_DOUT=" +
            std::to_string(wdOut) + " -DMIOPEN_N_DOUT_STRIDE=" + std::to_string(ndOutStride) +
            " -DMIOPEN_C_DOUT_STRIDE=" + std::to_string(cdOutStride) + " -DMIOPEN_H_DOUT_STRIDE=" +
            std::to_string(hdOutStride) + " -DMIOPEN_W_DOUT_STRIDE=" + std::to_string(wdOutStride) +
            " -DMIOPEN_IN_BLOCK_SZ=" + std::to_string(cIn * hIn * wIn) + " -DMIOPEN_OUT_BLOCK_SZ=" +
            std::to_string(cOut * hOut * wOut) + " -DMIOPEN_DIN_BLOCK_SZ=" +
            std::to_string(cdIn * hdIn * wdIn) + " -DMIOPEN_DOUT_BLOCK_SZ=" +
            std::to_string(cdOut * hdOut * wdOut);

        handle.AddKernel("miopenActivationBackward",
                         network_config,
                         program_name,
                         kernel_name,
                         vld,
                         vgd,
                         compiler_options)(dx,
                                           dy,
                                           x,
                                           y,
                                           as_float(f_diff_scale),
                                           as_float(f_activ_gamma),
                                           as_float(f_activ_beta),
                                           as_float(f_activ_alpha),
                                           static_cast<long long>(dxOffset),
                                           static_cast<long long>(dyOffset),
                                           static_cast<long long>(xOffset),
                                           static_cast<long long>(yOffset));
    });
    return (status);
}
} // namespace miopen
