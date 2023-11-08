// <ExecutePipePipe.cpp> -*- C++ -*-

#include "sparta/utils/SpartaAssert.hpp"
#include "sparta/utils/LogUtils.hpp"
#include "ExecutePipe.hpp"
#include "CoreUtils.hpp"

namespace olympia
{
    core_types::RegFile determineRegisterFile(const std::string & target_name)
    {
        if(target_name == "alu" || target_name == "br") {
            return core_types::RF_INTEGER;
        }
        else if(target_name == "fpu") {
            return core_types::RF_FLOAT;
        }
        sparta_assert(false, "Not supported this target: " << target_name);
    }

    const char ExecutePipe::name[] = "exe_pipe";

    ExecutePipe::ExecutePipe(sparta::TreeNode * node,
                             const ExecutePipeParameterSet * p) :
        sparta::Unit(node),
        ignore_inst_execute_time_(p->ignore_inst_execute_time),
        execute_time_(p->execute_time),
        scheduler_size_(p->scheduler_size),
        in_order_issue_(p->in_order_issue),
        reg_file_(determineRegisterFile(node->getGroup())),
        collected_inst_(node, node->getName())
    {
        // in_execute_inst_.
        //     registerConsumerHandler(CREATE_SPARTA_HANDLER_WITH_DATA(ExecutePipe, getInstsFromDispatch_,
        //                                                             InstPtr));

        in_reorder_flush_.
            registerConsumerHandler(CREATE_SPARTA_HANDLER_WITH_DATA(ExecutePipe, flushInst_,
                                                                    FlushManager::FlushingCriteria));
        // Startup handler for sending initiatl credits
        sparta::StartupEvent(node, CREATE_SPARTA_HANDLER(ExecutePipe, setupExecutePipe_));
        // Set up the precedence between issue and complete
        // Complete should come before issue because it schedules issue with a 0 cycle delay
        // issue should always schedule complete with a non-zero delay (which corresponds to the
        // insturction latency)
        complete_inst_ >> issue_inst_;

        ILOG("ExecutePipe construct: #" << node->getGroupIdx());

    }

    void ExecutePipe::setupExecutePipe_()
    {
        // Setup scoreboard view upon register file
        std::vector<core_types::RegFile> reg_files = {core_types::RF_INTEGER, core_types::RF_FLOAT};
        for(const auto rf : reg_files)
        {
            scoreboard_views_[rf].reset(new sparta::ScoreboardView(getContainer()->getName(),
                                                                   core_types::regfile_names[rf],
                                                                   getContainer()));
        }

        // Send initial credits
        out_scheduler_credits_.send(scheduler_size_);
    }

    // Called by the scheduler, scheduled by complete_inst_.
    void ExecutePipe::completeInst_(const InstPtr & ex_inst)
    {
        ex_inst->setStatus(Inst::Status::COMPLETED);
        ILOG("Completing inst: " << ex_inst);

        // set scoreboard
        if(SPARTA_EXPECT_FALSE(ex_inst->isTransfer()))
        {
            if(ex_inst->getPipe() == InstArchInfo::TargetPipe::I2F)
            {
                // Integer source -> FP dest -- need to mark the appropriate destination SB
                sparta_assert(reg_file_ == core_types::RegFile::RF_INTEGER,
                              "Got an I2F instruction in an ExecutionPipe that does not source the integer RF: " << ex_inst);
                const auto & dest_bits = ex_inst->getDestRegisterBitMask(core_types::RegFile::RF_FLOAT);
                scoreboard_views_[core_types::RegFile::RF_FLOAT]->setReady(dest_bits);
            }
            else {
                // FP source -> Integer dest -- need to mark the appropriate destination SB
                sparta_assert(ex_inst->getPipe() == InstArchInfo::TargetPipe::F2I,
                              "Instruction is marked transfer type, but I2F nor F2I: " << ex_inst);
                sparta_assert(reg_file_ == core_types::RegFile::RF_FLOAT,
                              "Got an F2I instruction in an ExecutionPipe that does not source the Float RF: " << ex_inst);
                const auto & dest_bits = ex_inst->getDestRegisterBitMask(core_types::RegFile::RF_INTEGER);
                scoreboard_views_[core_types::RegFile::RF_INTEGER]->setReady(dest_bits);
            }
        }
        else {
            const auto & dest_bits = ex_inst->getDestRegisterBitMask(reg_file_);
            scoreboard_views_[reg_file_]->setReady(dest_bits);
        }

        // We're not busy anymore
        unit_busy_ = false;

        // Count the instruction as completely executed
        ++total_insts_executed_;

        // Schedule issue if we have instructions to issue
        if (ready_queue_.size() > 0) {
            issue_inst_.schedule(sparta::Clock::Cycle(0));
        }
    }

    void ExecutePipe::flushInst_(const FlushManager::FlushingCriteria & criteria)
    {
        ILOG("Got flush for criteria: " << criteria);

        // Flush instructions in the ready queue
        ReadyQueue::iterator it = ready_queue_.begin();
        uint32_t credits_to_send = 0;
        while(it != ready_queue_.end()) {
            if((*it)->getUniqueID() >= uint64_t(criteria)) {
                ready_queue_.erase(it++);
                ++credits_to_send;
            }
            else {
                ++it;
            }
        }
        if(credits_to_send) {
            out_scheduler_credits_.send(credits_to_send, 0);
        }

        // Cancel outstanding instructions awaiting completion and
        // instructions on their way to issue
        auto cancel_critera = [criteria](const InstPtr & inst) -> bool {
            if(inst->getUniqueID() >= uint64_t(criteria)) {
                return true;
            }
            return false;
        };
        complete_inst_.cancelIf(cancel_critera);
        issue_inst_.cancel();

        if(complete_inst_.getNumOutstandingEvents() == 0) {
            unit_busy_ = false;
            collected_inst_.closeRecord();
        }
    }

}
