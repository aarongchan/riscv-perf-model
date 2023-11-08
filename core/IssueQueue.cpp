// <IssueQueue.cpp> -*- C++ -*-

#include "sparta/utils/SpartaAssert.hpp"
#include "sparta/utils/LogUtils.hpp"
#include "IssueQueue.hpp"
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

    const char IssueQueue::name[] = "issue_queue";

    IssueQueue::IssueQueue(sparta::TreeNode * node,
                                const IssueQueueParameterSet * p) :
        sparta::Unit(node),
        ignore_inst_execute_time_(p->ignore_inst_execute_time),
        execute_time_(p->execute_time),
        scheduler_size_(p->scheduler_size),
        in_order_issue_(p->in_order_issue),
        reg_file_(determineRegisterFile(node->getGroup())),
        collected_inst_(node, node->getName())
    {
        in_execute_inst_.
            registerConsumerHandler(CREATE_SPARTA_HANDLER_WITH_DATA(IssueQueue, getInstsFromDispatch_,
                                                                    InstPtr));
        // Startup handler for sending initiatl credits
        sparta::StartupEvent(node, CREATE_SPARTA_HANDLER(ExecutePipe, setupIssueQueue_));
        // Set up the precedence between issue and complete
        // Complete should come before issue because it schedules issue with a 0 cycle delay
        // issue should always schedule complete with a non-zero delay (which corresponds to the
        // insturction latency)
        complete_inst_ >> issue_inst_;

        ILOG("ExecutePipe construct: #" << node->getGroupIdx());

    }

    void IssueQueue::setupIssueQueue_()
    {
        // Setup scoreboard view upon register file
        std::vector<core_types::RegFile> reg_files = {core_types::RF_INTEGER, core_types::RF_FLOAT};
        for(const auto rf : reg_files)
        {
            scoreboard_views_[rf].reset(new sparta::ScoreboardView(getContainer()->getName(),
                                                                    core_types::regfile_names[rf],
                                                                    getContainer()));
        }

        // Setup Issue Queues based on topology defined in .yaml
        void IssueQueueFactory::onConfiguring(sparta::ResourceTreeNode* node)
        {
            auto issue_queue_topology = olympia::coreutils::getExecutionTopology(node->getParent());
            for (auto issue_queue_pair : issue_queue_topology)
            {
                // go through issue queues and map
                // an issue queue to execution pipe/units
                const auto issue_queue_name = issue_queue_pair[0];
                std::vector<auto> exe_units;
                for(int i = 1; i < issue_queue_pair.size(); ++i){
                    exe_units.push_back(issue_queue_pair[i]);
                }
                issue_queue_mapping_[issue_queue_name] = exe_units;
                
                ReadyQueue  ready_queue_;
                issue_queue_[issue_queue_name] = typedef std::list<InstPtr> ReadyQueue;
                // const auto unit_count = exe_unit_pair[1];
                // const auto exe_idx    = (unsigned int) std::stoul(unit_count);
                // sparta_assert(exe_idx > 0, "Expected more than 0 units! " << tgt_name);
                // for(uint32_t unit_num = 0; unit_num < exe_idx; ++unit_num)
                // {
                //     const std::string unit_name = tgt_name + std::to_string(unit_num);
                //     exe_pipe_tns_.emplace_back(new sparta::ResourceTreeNode(node,
                //                                                             unit_name,
                //                                                             tgt_name,
                //                                                             unit_num,
                //                                                             std::string(unit_name + " Execution Pipe"),
                //                                                             &exe_pipe_fact_));
                // }
            }
            // Send initial credits
            out_scheduler_credits_.send(scheduler_size_);
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // Callbacks
    void IssueQueue::getInstsFromDispatch_(const InstPtr & ex_inst)
    {
        // FIXME: Now every source operand should be ready
        const auto & src_bits = ex_inst->getSrcRegisterBitMask(reg_file_);
        if(scoreboard_views_[reg_file_]->isSet(src_bits))
        {
            // Insert at the end if we are doing in order issue or if the scheduler is empty
            ILOG("Sending to issue queue " << ex_inst);
            if (in_order_issue_ == true || ready_queue_.size() == 0) {
                ready_queue_.emplace_back(ex_inst);
            }
            else {
                // Stick the instructions in a random position in the ready queue
                uint64_t issue_pos = uint64_t(std::rand()) % ready_queue_.size();
                if (issue_pos == ready_queue_.size()-1) {
                    ready_queue_.emplace_back(ex_inst);
                }
                else {
                    uint64_t pos = 0;
                    auto iter = ready_queue_.begin();
                    while (iter != ready_queue_.end()) {
                        if (pos == issue_pos) {
                            ready_queue_.insert(iter, ex_inst);
                            break;
                        }
                        ++iter;
                        ++pos;
                    }
                }
            }
            // Schedule issue if the alu is not busy
            if (unit_busy_ == false) {
                issue_inst_.schedule(sparta::Clock::Cycle(0));
            }
        }
        else{
            scoreboard_views_[reg_file_]->
                registerReadyCallback(src_bits, ex_inst->getUniqueID(),
                                        [this, ex_inst](const sparta::Scoreboard::RegisterBitMask&)
                                        {
                                            this->getInstsFromDispatch_(ex_inst);
                                        });
            ILOG("Instruction NOT ready: " << ex_inst << " Bits needed:" << sparta::printBitSet(src_bits));
        }
    }

    void IssueQueue::issueInst_()
    {
        // Issue a random instruction from the ready queue
        sparta_assert_context(unit_busy_ == false && ready_queue_.size() > 0,
                                "Somehow we're issuing on a busy unit or empty ready_queue");
        // Issue the first instruction
        InstPtr & ex_inst_ptr = ready_queue_.front();
        auto & ex_inst = *ex_inst_ptr;
        ex_inst.setStatus(Inst::Status::SCHEDULED);
        const uint32_t exe_time =
            ignore_inst_execute_time_ ? execute_time_ : ex_inst.getExecuteTime();
        collected_inst_.collectWithDuration(ex_inst, exe_time);
        ILOG("Executing: " << ex_inst << " for "
                << exe_time + getClock()->currentCycle());
        sparta_assert(exe_time != 0);

        ++total_insts_issued_;
        // Mark the instruction complete later...
        complete_inst_.preparePayload(ex_inst_ptr)->schedule(exe_time);
        // Mark the alu as busy
        unit_busy_ = true;
        // Pop the insturction from the scheduler and send a credit back to dispatch
        ready_queue_.pop_front();
        out_scheduler_credits_.send(1, 0);
    }

}
