#include "sparta/utils/SpartaAssert.hpp"
#include "CoreUtils.hpp"
#include "VLSU.hpp"
#include "LSU.hpp"
#include "sparta/simulation/Unit.hpp"
#include <string>

#include "OlympiaAllocators.hpp"

namespace olympia
{
    const char VLSU::name[] = "VLSU";

    ////////////////////////////////////////////////////////////////////////////////
    // Constructor
    ////////////////////////////////////////////////////////////////////////////////

    VLSU::VLSU(sparta::TreeNode* node, const VLSUParameterSet* p) :
        LSU(node, p, true)
    {
        sparta_assert(p->mmu_lookup_stage_length > 0,
                      "MMU lookup stage should atleast be one cycle");
        sparta_assert(p->cache_read_stage_length > 0,
                      "Cache read stage should atleast be one cycle");
        sparta_assert(p->cache_lookup_stage_length > 0,
                      "Cache lookup stage should atleast be one cycle");

        // Startup handler for sending initial credits
        sparta::StartupEvent(node, CREATE_SPARTA_HANDLER(LSU, sendInitialCredits_));

        // Port config
        in_vlsu_insts_.registerConsumerHandler(
            CREATE_SPARTA_HANDLER_WITH_DATA(LSU, getInstsFromDispatch_, InstPtr));

        // in_reorder_flush_.registerConsumerHandler(
        //     CREATE_SPARTA_HANDLER_WITH_DATA(VLSU, handleFlush_, FlushManager::FlushingCriteria));

        // in_mmu_lookup_req_.registerConsumerHandler(
        //     CREATE_SPARTA_HANDLER_WITH_DATA(VLSU, handleMMUReadyReq_, MemoryAccessInfoPtr));

        // in_mmu_lookup_ack_.registerConsumerHandler(
        //     CREATE_SPARTA_HANDLER_WITH_DATA(VLSU, getAckFromMMU_, MemoryAccessInfoPtr));

        // in_cache_lookup_req_.registerConsumerHandler(
        //     CREATE_SPARTA_HANDLER_WITH_DATA(VLSU, handleCacheReadyReq_, MemoryAccessInfoPtr));

        // in_cache_lookup_ack_.registerConsumerHandler(
        //     CREATE_SPARTA_HANDLER_WITH_DATA(VLSU, getAckFromCache_, MemoryAccessInfoPtr));

        // // Allow the pipeline to create events and schedule work
        // ldst_pipeline_.performOwnUpdates();

        // // There can be situations where NOTHING is going on in the
        // // simulator but forward progression of the pipeline elements.
        // // In this case, the internal event for the LS pipeline will
        // // be the only event keeping simulation alive.  Sparta
        // // supports identifying non-essential events (by calling
        // // setContinuing to false on any event).
        // ldst_pipeline_.setContinuing(true);

        // ldst_pipeline_.registerHandlerAtStage(
        //     address_calculation_stage_, CREATE_SPARTA_HANDLER(VLSU, handleAddressCalculation_));

        // ldst_pipeline_.registerHandlerAtStage(mmu_lookup_stage_,
        //                                       CREATE_SPARTA_HANDLER(VLSU, handleMMULookupReq_));

        // ldst_pipeline_.registerHandlerAtStage(cache_lookup_stage_,
        //                                       CREATE_SPARTA_HANDLER(VLSU, handleCacheLookupReq_));

        // ldst_pipeline_.registerHandlerAtStage(cache_read_stage_,
        //                                       CREATE_SPARTA_HANDLER(VLSU, handleCacheRead_));

        ldst_pipeline_.registerHandlerAtStage(complete_stage_,
                                              CREATE_SPARTA_HANDLER(VLSU, VLSU::completeInst_));

        ILOG("VLSU construct: #" << node->getGroupIdx());
    }
    VLSU::~VLSU()
    {
        DLOG(getContainer()->getLocation() << ": " << load_store_info_allocator_.getNumAllocated()
                                           << " LoadStoreInstInfo objects allocated/created");
        DLOG(getContainer()->getLocation() << ": " << memory_access_allocator_.getNumAllocated()
                                           << " MemoryAccessInfo objects allocated/created");
    }
    void VLSU::onStartingTeardown_()
    {
        // If ROB has not stopped the simulation &
        // the ldst has entries to process we should fail
        if ((false == rob_stopped_simulation_) && (false == ldst_inst_queue_.empty()))
        {
            dumpDebugContent_(std::cerr);
            sparta_assert(false, "Issue queue has pending instructions");
        }
    }

    // Issue/Re-issue ready instructions in the issue queue
    void VLSU::issueInst_()
    {
        // Instruction issue arbitration
        const LoadStoreInstInfoPtr win_ptr = arbitrateInstIssue_();
        // NOTE:
        // win_ptr should always point to an instruction ready to be issued
        // Otherwise assertion error should already be fired in arbitrateInstIssue_()
        if(win_ptr != nullptr){
            ++lsu_insts_issued_;
            // Append load/store pipe
            ldst_pipeline_.append(win_ptr);

            // if the element width is greater than data width, we can only pull data width then
            uint32_t width = data_width_ < win_ptr->getInstPtr()->getEEW() ? data_width_ : win_ptr->getInstPtr()->getEEW();
            // Set total number of vector iterations
            win_ptr->setTotalVectorIter(Inst::VLEN/width);
            ILOG("Setting vector instruction data width: " << width)
            // We append to replay queue to prevent ref count of the shared pointer to drop before
            // calling pop below
            if (allow_speculative_load_exec_)
            {
                ILOG("Appending to replay queue " << win_ptr);
                appendToReplayQueue_(win_ptr);
            }

            // Remove inst from ready queue
            win_ptr->setInReadyQueue(false);

            // Update instruction issue info
            win_ptr->setState(LoadStoreInstInfo::IssueState::ISSUED);
            win_ptr->setPriority(LoadStoreInstInfo::IssuePriority::LOWEST);

            // Schedule another instruction issue event if possible
            if (isReadyToIssueInsts_())
            {
                ILOG("IssueInst_ issue");
                uev_issue_inst_.schedule(sparta::Clock::Cycle(1));
            }
        }
    }
    // Retire load/store instruction
    void VLSU::completeInst_()
    {
        // Check if flushing event occurred just now
        if (!ldst_pipeline_.isValid(complete_stage_))
        {
            return;
        }
        const LoadStoreInstInfoPtr & load_store_info_ptr = ldst_pipeline_[complete_stage_];
        uint32_t total_iters = load_store_info_ptr->getTotalVectorIter();
        // we're done load/storing all vector bits, can complete
        const MemoryAccessInfoPtr & mem_access_info_ptr =
        load_store_info_ptr->getMemoryAccessInfoPtr();

        if (false == mem_access_info_ptr->isDataReady())
        {
            ILOG("Cannot complete inst, cache data is missing: " << mem_access_info_ptr);
            return;
        }
        else
        {
            if(load_store_info_ptr->getVectorIter() >= total_iters){

                const InstPtr & inst_ptr = mem_access_info_ptr->getInstPtr();
                const bool is_store_inst = inst_ptr->isStoreInst();
                ILOG("Completing inst: " << inst_ptr);
                ILOG(mem_access_info_ptr);

                core_types::RegFile reg_file = core_types::RF_INTEGER;
                const auto & dests = inst_ptr->getDestOpInfoList();
                if (dests.size() > 0)
                {
                    sparta_assert(dests.size() == 1); // we should only have one destination
                    reg_file = olympia::coreutils::determineRegisterFile(dests[0]);
                    const auto & dest_bits = inst_ptr->getDestRegisterBitMask(reg_file);
                    scoreboard_views_[reg_file]->setReady(dest_bits);
                }

                // Complete load instruction
                if (!is_store_inst)
                {
                    sparta_assert(mem_access_info_ptr->getCacheState() == MemoryAccessInfo::CacheState::HIT,
                                "Load instruction cannot complete when cache is still a miss! "
                                    << mem_access_info_ptr);

                    if (isReadyToIssueInsts_())
                    {
                        ILOG("Complete issue");
                        uev_issue_inst_.schedule(sparta::Clock::Cycle(0));
                    }
                    if (load_store_info_ptr->isRetired()
                        || inst_ptr->getStatus() == Inst::Status::COMPLETED)
                    {
                        ILOG("Load was previously completed or retired " << load_store_info_ptr);
                        if (allow_speculative_load_exec_)
                        {
                            ILOG("Removed replay " << inst_ptr);
                            removeInstFromReplayQueue_(load_store_info_ptr);
                        }
                        return;
                    }

                    // Mark instruction as completed
                    inst_ptr->setStatus(Inst::Status::COMPLETED);
                    if (inst_ptr->isUOp())
                    {
                        sparta_assert(!inst_ptr->getUOpParent().expired(),
                                    "UOp instruction parent shared pointer is expired");
                        auto shared_ex_inst = inst_ptr->getUOpParent().lock();
                        shared_ex_inst->incrementUOpDoneCount();
                    }
                    // Remove completed instruction from queues
                    ILOG("Removed issue queue " << inst_ptr);
                    popIssueQueue_(load_store_info_ptr);

                    if (allow_speculative_load_exec_)
                    {
                        ILOG("Removed replay " << inst_ptr);
                        removeInstFromReplayQueue_(load_store_info_ptr);
                    }

                    lsu_insts_completed_++;
                    out_lsu_credits_.send(1, 0);

                    ILOG("Complete Load Instruction: " << inst_ptr->getMnemonic() << " uid("
                                                    << inst_ptr->getUniqueID() << ")");

                    return;
                }

                // Complete store instruction
                if (inst_ptr->getStatus() != Inst::Status::RETIRED)
                {

                    sparta_assert(mem_access_info_ptr->getMMUState() == MemoryAccessInfo::MMUState::HIT,
                                "Store instruction cannot complete when TLB is still a miss!");

                    ILOG("Store was completed but waiting for retire " << load_store_info_ptr);

                    if (isReadyToIssueInsts_())
                    {
                        ILOG("Store complete issue");
                        uev_issue_inst_.schedule(sparta::Clock::Cycle(0));
                    }
                }
                // Finish store operation
                else
                {
                    sparta_assert(mem_access_info_ptr->getCacheState() == MemoryAccessInfo::CacheState::HIT,
                                "Store inst cannot finish when cache is still a miss! " << inst_ptr);

                    sparta_assert(mem_access_info_ptr->getMMUState() == MemoryAccessInfo::MMUState::HIT,
                                "Store inst cannot finish when cache is still a miss! " << inst_ptr);
                    if (isReadyToIssueInsts_())
                    {
                        ILOG("Complete store issue");
                        uev_issue_inst_.schedule(sparta::Clock::Cycle(0));
                    }

                    if (!load_store_info_ptr->getIssueQueueIterator().isValid())
                    {
                        ILOG("Inst was already retired " << load_store_info_ptr);
                        if (allow_speculative_load_exec_)
                        {
                            ILOG("Removed replay " << load_store_info_ptr);
                            removeInstFromReplayQueue_(load_store_info_ptr);
                        }
                        return;
                    }

                    ILOG("Removed issue queue " << inst_ptr);
                    popIssueQueue_(load_store_info_ptr);

                    if (allow_speculative_load_exec_)
                    {
                        ILOG("Removed replay " << load_store_info_ptr);
                        removeInstFromReplayQueue_(load_store_info_ptr);
                    }

                    lsu_insts_completed_++;
                    out_lsu_credits_.send(1, 0);

                    ILOG("Store operation is done!");
                    if (inst_ptr->isUOp())
                    {
                        sparta_assert(!inst_ptr->getUOpParent().expired(),
                                    "UOp instruction parent shared pointer is expired");
                        auto shared_ex_inst = inst_ptr->getUOpParent().lock();
                        shared_ex_inst->incrementUOpDoneCount();
                    }
                }

                // NOTE:
                // Checking whether an instruction is ready to complete could be non-trivial
                // Right now we simply assume:
                // (1)Load inst is ready to complete as long as both MMU and cache access finish
                // (2)Store inst is ready to complete as long as MMU (address translation) is done
            }
            else{
                //const InstPtr & inst_ptr = mem_access_info_ptr->getInstPtr();
                // queue up next iteration, increment address with stride or index. Keep same instruction pointer.
                sparta::memory::addr_t addr = load_store_info_ptr->getInstPtr()->getTargetVAddr();
                // increment base address by EEW
                load_store_info_ptr->getInstPtr()->setTargetVAddr(addr + load_store_info_ptr->getInstPtr()->getStride());
                // increment vector LSU count
                uint32_t vector_iter = load_store_info_ptr->getVectorIter();
                ILOG("Multiple passes needed for VLSU, pass number " << vector_iter << " of " << total_iters);
                load_store_info_ptr->setVectorIter(++vector_iter);
                
                bool iterate = true;
                for (const auto & inst : ready_queue_)
                {
                    if(inst == load_store_info_ptr){
                        iterate = false;
                        break;
                    }
                }
                // for (const auto & ldst_inst : ldst_inst_queue_)
                // {
                //     if (ldst_inst->getInstPtr() == inst_ptr)
                //     {
                //         iterate = false;
                //         break;
                //     }
                // }
                // we remove from replay because we should be done speculating, for futher iterations we don't need to
                // speculate because should be a cache hit and address generation is straight forward
                if(iterate){
                    if(allow_speculative_load_exec_)
                    {
                        removeInstFromReplayQueue_(load_store_info_ptr->getInstPtr());
                    }
                    appendToReadyQueue_(load_store_info_ptr);
                    uev_issue_inst_.schedule(sparta::Clock::Cycle(0));
                }
            }
        }
    }
} // namespace olympia
