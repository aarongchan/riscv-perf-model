// <IssueQueue.hpp> -*- C++ -*-

/**
 * \file  IssueQueue.hpp
 * \brief Definition of an Issue Queue
 *
 *
 */

#pragma once


#include "sparta/ports/PortSet.hpp"
#include "sparta/ports/DataPort.hpp"
#include "sparta/events/EventSet.hpp"
#include "sparta/events/UniqueEvent.hpp"
#include "sparta/events/StartupEvent.hpp"
#include "sparta/simulation/TreeNode.hpp"
#include "sparta/simulation/Unit.hpp"
#include "sparta/simulation/ParameterSet.hpp"
#include "sparta/simulation/Clock.hpp"
#include "sparta/simulation/ResourceFactory.hpp"
#include "sparta/collection/Collectable.hpp"
#include "sparta/resources/Scoreboard.hpp"

#include "Inst.hpp"
#include "CoreTypes.hpp"
#include "FlushManager.hpp"


namespace olympia
{
    /**
     * \class IssueQueue
     * \brief Defines the stages for an execution pipe
     */
    class IssueQueue : public sparta::Unit
    {
    public:
        //! \brief Parameters for IssueQueue model
        class IssueQueueParameterSet : public sparta::ParameterSet
        {
        public:
            IssueQueueParameterSet(sparta::TreeNode* n) :
                sparta::ParameterSet(n)
            { }
        };
        /**
         * @brief Constructor for IssueQueue
         *
         * @param node The node that represents (has a pointer to) the IssueQueue
         * @param p The IssueQueue's parameter set
         */
        IssueQueue(sparta::TreeNode * node,
                const IssueQueueParameterSet * p);
        //! \brief Name of this resource. Required by sparta::UnitFactory
        static const char name[];
        // Events used to issue and complete the instruction
        sparta::UniqueEvent<> issue_inst_{&unit_event_set_, getName() + "_issue_inst",
                CREATE_SPARTA_HANDLER(IssueQueue, issueInst_)};

        ////////////////////////////////////////////////////////////////////////////////
        // Callbacks
        void issueInst_();
        void getInstsFromDispatch_(const InstPtr&);

        // Friend class used for testing
        friend class IssueQueueTester;
    private:
        std::unordered_map<std::string,std::vector<std::string>> issue_queue_mapping_;
        typedef std::list<InstPtr> ReadyQueue;

        std::unordered_map<std::string, ReadyQueue> issue_queue_;
    };
    // //! IssueQueue's factory class.  Don't create IssueQueue without it
    // class IssueQueueFactory : public sparta::ResourceFactory<IssueQueue, IssueQueue::IssueQueueParameterSet>
    // {
    // public:
    //     void onConfiguring(sparta::ResourceTreeNode* node) override;
    //     void deleteSubtree(sparta::ResourceTreeNode*) override;

    //     ~IssueQueueFactory() = default;
    // private:

    //     // The order of these two members is VERY important: you
    //     // must destroy the tree nodes _before_ the factory since
    //     // the factory is used to destroy the nodes!
    //     IssueQueueFactory issue_queue_fact_;
    //     std::vector<std::unique_ptr<sparta::ResourceTreeNode>> issue_queue_tns_;
    // };

    class IssueQueueTester;
    using IssueQueueFactory = sparta::ResourceFactory<olympia::IssueQueue,
                                                       olympia::IssueQueue::IssueQueueParameterSet>;
    

}