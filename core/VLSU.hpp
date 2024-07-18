
#pragma once

#include "sparta/ports/PortSet.hpp"
#include "sparta/ports/SignalPort.hpp"
#include "sparta/ports/DataPort.hpp"
#include "sparta/events/EventSet.hpp"
#include "sparta/events/UniqueEvent.hpp"
#include "sparta/simulation/Unit.hpp"
#include "sparta/simulation/ParameterSet.hpp"
#include "sparta/simulation/TreeNode.hpp"
#include "sparta/collection/Collectable.hpp"
#include "sparta/events/StartupEvent.hpp"
#include "sparta/resources/Pipeline.hpp"
#include "sparta/resources/Buffer.hpp"
#include "sparta/resources/PriorityQueue.hpp"
#include "sparta/pairs/SpartaKeyPairs.hpp"
#include "sparta/simulation/State.hpp"
#include "sparta/utils/SpartaSharedPointer.hpp"
#include "sparta/utils/LogUtils.hpp"
#include "sparta/resources/Scoreboard.hpp"

#include "cache/TreePLRUReplacement.hpp"

#include "Inst.hpp"
#include "CoreTypes.hpp"
#include "FlushManager.hpp"
#include "CacheFuncModel.hpp"
#include "MemoryAccessInfo.hpp"
#include "LoadStoreInstInfo.hpp"
#include "MMU.hpp"
#include "DCache.hpp"

namespace olympia
{
    class VLSU : public LSU
    {
      public:
        /*!
         * \class VLSUParameterSet
         * \brief Parameters for VLSU model
         */
        class VLSUParameterSet : public LSUParameterSet
        {
          public:
            //! Constructor for VLSUParameterSet
            VLSUParameterSet(sparta::TreeNode* n) : LSUParameterSet(n) {}
        };

        /*!
         * \brief Constructor for VLSU
         * \note  node parameter is the node that represent the VLSU and
         *        p is the VLSU parameter set
         */
        VLSU(sparta::TreeNode* node, const VLSUParameterSet* p);

        //! Destroy the VLSU
        ~VLSU();

        //! name of this resource.
        static const char name[];

        ////////////////////////////////////////////////////////////////////////////////
        // Type Name/Alias Declaration
        ////////////////////////////////////////////////////////////////////////////////

        using LoadStoreInstInfoPtr = sparta::SpartaSharedPointer<LoadStoreInstInfo>;
        using LoadStoreInstIterator = sparta::Buffer<LoadStoreInstInfoPtr>::const_iterator;

        using FlushCriteria = FlushManager::FlushingCriteria;

      private:
        ////////////////////////////////////////////////////////////////////////////////
        // Input Ports
        ////////////////////////////////////////////////////////////////////////////////
        sparta::DataInPort<InstQueue::value_type> in_vlsu_insts_{&unit_port_set_, "in_vlsu_insts", 1};
        
        ////////////////////////////////////////////////////////////////////////////////
        // Internal States
        ////////////////////////////////////////////////////////////////////////////////

        // Issue/Re-issue ready instructions in the issue queue
        void issueInst_();

        // Retire load/store instruction
        void completeInst_();

        // When simulation is ending (error or not), this function
        // will be called
        void onStartingTeardown_() override;

        friend class VLSUTester;
    };

    class VLSUTester;
} // namespace olympia
