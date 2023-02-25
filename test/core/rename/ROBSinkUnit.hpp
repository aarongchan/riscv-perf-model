
#pragma once

#include "core/InstGenerator.hpp"

#include "sparta/simulation/TreeNode.hpp"
#include "sparta/statistics/StatisticSet.hpp"
#include "sparta/events/SingleCycleUniqueEvent.hpp"
#include "sparta/utils/SpartaSharedPointer.hpp"
#include "sparta/utils/LogUtils.hpp"

#include <string>

namespace rename_test
{

    ////////////////////////////////////////////////////////////////////////////////
    // "Sink" unit, just sinks instructions sent to it.  Sends credits
    // back as directed by params/execution mode
    class ROBSinkUnit : public sparta::Unit
    {
    public:
        static constexpr char name[] = "ROBSinkUnit";

        class ROBSinkUnitParameters : public sparta::ParameterSet
        {
        public:
            explicit ROBSinkUnitParameters(sparta::TreeNode *n) :
                sparta::ParameterSet(n)
            { }
            PARAMETER(uint32_t, sink_queue_size, 10, "Sink queue size for testing")
            PARAMETER(std::string, purpose, "grp", "Purpose of this ROBSinkUnit: grp, single")
        };

        ROBSinkUnit(sparta::TreeNode * n, const ROBSinkUnitParameters * params) :
            sparta::Unit(n),
            credits_(params->sink_queue_size),
            credits_to_send_back_(credits_)
        {
            if(params->purpose == "grp") {
                in_sink_inst_.registerConsumerHandler
                    (CREATE_SPARTA_HANDLER_WITH_DATA(ROBSinkUnit, sinkInst_<olympia::InstGroupPtr>, olympia::InstGroupPtr));
            }
            else {
                in_sink_inst_.registerConsumerHandler
                    (CREATE_SPARTA_HANDLER_WITH_DATA(ROBSinkUnit, sinkInst_<olympia::InstPtr>, olympia::InstPtr));
            }
            sparta::StartupEvent(n, CREATE_SPARTA_HANDLER(ROBSinkUnit, sendCredits_));
        }

    private:

        template<class InstType>
        void sinkInst_(const InstType & inst_or_insts)
        {
            --credits_;
            if constexpr(std::is_same_v<InstType, olympia::InstGroupPtr>)
            {
                for(auto ptr : *inst_or_insts) {
                    ILOG("Instruction: '" << ptr << "' sinked");
                    //out_rob_retire_ack_.send(ptr);
                }
            }
            else {
                ILOG("Instruction: '" << inst_or_insts << "' sinked");
            }
            ++credits_to_send_back_;
            ev_return_credits_.schedule(1);
        }

        void sendCredits_() {
            out_sink_credits_.send(credits_to_send_back_);
            credits_ += credits_to_send_back_;
            credits_to_send_back_ = 0;
        }

        sparta::DataOutPort<uint32_t>             out_sink_credits_ {&unit_port_set_, "out_sink_credits"};
        sparta::DataInPort<olympia::InstPtr>      in_sink_inst_     {&unit_port_set_, "in_sink_inst",
                                                                     sparta::SchedulingPhase::Tick, 1};
        sparta::DataInPort<olympia::InstGroupPtr> in_sink_inst_grp_ {&unit_port_set_, "in_sink_inst_grp",
                                                                     sparta::SchedulingPhase::Tick, 1};
        //sparta::DataOutPort<olympia::InstPtr>     out_rob_retire_ack_ {&unit_port_set_, "out_rob_retire_ack"};
        uint32_t credits_ = 0;
        sparta::UniqueEvent<> ev_return_credits_{&unit_event_set_, "return_credits",
                                                 CREATE_SPARTA_HANDLER(ROBSinkUnit, sendCredits_)};
        uint32_t credits_to_send_back_ = 0;
    };

    using ROBSinkUnitFactory = sparta::ResourceFactory<ROBSinkUnit, ROBSinkUnit::ROBSinkUnitParameters>;
}
