//########################################################################
// (C) Candera GmbH
// All rights reserved.
// -----------------------------------------------------
// This document contains proprietary information belonging to
// Candera GmbH.
// Passing on and copying of this document, use and communication
// of its contents is not permitted without prior written authorization.
//########################################################################
#include "ActiveAnimationsManager.h"
#include <Application/System/UpdateSystem.h>
#include <Application/Logging/AppLogging.h>
#include <Application/System/Animation/StateMachineBasedAnimationSupport.h>

namespace App {
    FEATSTD_LOG_SET_REALM(App::AppLog);

    const Candera::TimeType cTenSeconds = 10000U;
    //Due Candera Map structur does not have iterators, Visitor to iteraits on elements is needed.
    class UpdateAnimationsVisitor : public ActiveAnimationsManager::ActiveAnimationsMap::Visitor {
    public:


        UpdateAnimationsVisitor(const Candera::TimeType& currentTime, const Candera::TimeType& passedMs, bool prepareCleanedMap) : m_currentTime(currentTime), m_passedMs(passedMs), m_activeStoryBoardCount(0U), m_storyBoardCount(0U), m_prepareCleanedMap(prepareCleanedMap){};

        virtual bool Visit(const ActiveAnimationsManager::StoryBoardPtr& key, ActiveAnimationsManager::ScenesVec& scenesVector) override {

            ++m_storyBoardCount;

            bool toDelete = true;
            for (FeatStd::SizeType i = 0U; i < scenesVector.Size(); ++i) {
                if (scenesVector[i]->Update(m_currentTime, m_passedMs)) {
                    toDelete = false;       //checking are all storyboard animations state, if all are completed than this storyboard is marked as to delete.
                }
            }

            if (toDelete) { //if storyboard have all animations completed
                scenesVector.Clear(); //vector of scenes pointer can be cleared
                m_finishedStoryBoards.Add(key); //storyboard addrd to finished storyboards to be able inform those storyboards that the need to set state of activate to false.
            }
            else {
                ++m_activeStoryBoardCount; //storyboard animations still active so count it as active storyboard to be able check is cleanup needed.
                if (m_prepareCleanedMap) { //if due to heuristic, map cleanup needed.
                    m_cleanedMap.Insert(key, scenesVector); //than cleanedMap ( without any finished storyboard) need to be prepared for swap with old one.
                }
            }

            return true;
        }

        FeatStd::UInt16 GetActiveAnimationCount() {
            return m_activeStoryBoardCount;
        }

        ActiveAnimationsManager::ActiveAnimationsMap& GetCleanedActiveAnimationsMap() {
            return m_cleanedMap;
        }

        //Compares how many storyboard have active animations against all animations registered, including those which already finished but cant be removed from map due of missing performant delete method in map.
        bool IsGarbageOverflow() {
            return (m_activeStoryBoardCount * 5U) < m_storyBoardCount; //if 80% of map items is empty (dangling finished animations from storyboard) than return true.
        }

        void NotifyStoryBoards() {
            for (FeatStd::SizeType idx = 0U; idx < m_finishedStoryBoards.Size(); ++idx) {
                Candera::VwXml::StoryBoard* story = m_finishedStoryBoards[idx];

                if (nullptr != story && story->GetActive()) {
                    story->SetActive(false);
                    story->OnChanged(*story);  //inform storyboard that all its scenes are completed.

					if (story->GetInternalEndlessMode())
                    {
                        story->SetActive(true);
                        story->OnChanged(*story);
                    }
                }
            }
            m_finishedStoryBoards.Clear();
        }

    private:
        ActiveAnimationsManager::ActiveAnimationsMap m_cleanedMap;
        const Candera::TimeType& m_currentTime;
        const Candera::TimeType& m_passedMs;
        FeatStd::UInt16 m_activeStoryBoardCount;
        FeatStd::UInt16 m_storyBoardCount;
        Candera::Internal::Vector<Candera::VwXml::StoryBoard*> m_finishedStoryBoards;

        bool m_prepareCleanedMap;

    };

    ActiveAnimationsManager& ActiveAnimationsManager::GetInstance()
    {
        FEATSTD_SYNCED_STATIC_OBJECT(ActiveAnimationsManager, s_inst);
        return s_inst;
    }

    ActiveAnimationsManager::ActiveAnimationsManager() : m_cleanupOnNextCall(false), m_mapOverflowDurationCounter(0U)
    {
        App::UpdateSystem::Register<ActiveAnimationsManager, &ActiveAnimationsManager::TimeUpdater>(this);
    }

    ActiveAnimationsManager::~ActiveAnimationsManager()
    {
        App::UpdateSystem::Deregister(this);
    }

    bool ActiveAnimationsManager::RegisterAnimation(Candera::VwXml::Scene* scene) {
        EnqueueOperation([this, scene]() {
            Candera::VwXml::StoryBoard* storyBoard = SceneToStoryBoard(scene);
            if (storyBoard) {
                ScenesVec* res = m_activeAnimationsMap.Find(storyBoard);
                if (res) {
                    res->Add(scene);
                } else {
                    ScenesVec newVec;
                    newVec.Add(scene);
                    storyBoard->SetActive(true);
                    storyBoard->OnChanged(*storyBoard);
                    m_activeAnimationsMap.Insert(storyBoard, newVec);
                }
            } else {
                FEATSTD_LOG_ERROR("Failed to find Parent StoryBoard for the scene!");
            }
        });
        return true;
    }

    void ActiveAnimationsManager::UnregisterAnimation(Candera::VwXml::Scene* scene) {
        EnqueueOperation([this, scene]() {
            Candera::VwXml::StoryBoard* storyBoard = SceneToStoryBoard(scene);
            if (storyBoard) {
                m_activeAnimationsMap.Remove(storyBoard);
            }
        });
    }

    void ActiveAnimationsManager::UnregisterAnimation(Candera::VwXml::StoryBoard* storyBoard) {
        EnqueueOperation([this, storyBoard]() {
            if (storyBoard) {
                m_activeAnimationsMap.Remove(storyBoard);
            }
        });
    }

    bool ActiveAnimationsManager::IsAnimationRunning(const StoryBoardPtr storyBoard) const
    {
        const ScenesVec* vecResult = m_activeAnimationsMap.Find(storyBoard);
        if (nullptr != vecResult) {
            return !vecResult->Empty();
        }
        return false;
    }

    void ActiveAnimationsManager::EnqueueOperation(const std::function<void()>& operation) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_operationQueue.push(operation);
    }

    void ActiveAnimationsManager::ProcessQueue() {
        while (!m_operationQueue.empty()) {
            std::function<void()> operation;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                operation = m_operationQueue.front();
                m_operationQueue.pop();
            }
            if (operation) {
                operation();
            }
        }
    }

//private:
    void ActiveAnimationsManager::UpdateAnimations(const Candera::TimeType currentTime, const Candera::TimeType passedMs)
    {
        UpdateAnimationsVisitor updateVisitor(currentTime, passedMs, m_cleanupOnNextCall);
        m_activeAnimationsMap.Visit(updateVisitor);

        if (0U == updateVisitor.GetActiveAnimationCount()) { //If there is no active animations we can just clear whole activeAnimationsMap.
            m_activeAnimationsMap.Clear();
            m_mapOverflowDurationCounter = currentTime;
        }
        else if (m_cleanupOnNextCall) { //this if should never be true when everything with animations works correctly. Mean on app idle if there is no loading animation m_activeAnimationMap should have 0 active animations and Clear should be done.
            m_activeAnimationsMap = updateVisitor.GetCleanedActiveAnimationsMap(); //If situation like this happens, to reduce iteration through map with tens or hundrets of empty values. We collect active animations only to new map and replace it with active animations map to get rid of empty keys.
            m_mapOverflowDurationCounter = currentTime;
            FEATSTD_LOG_INFO("Active animation map cleanup done! It should not be needed. Please analyze dangling animations");
        }

        m_cleanupOnNextCall = IsCleanupNeededHeuristic(updateVisitor.IsGarbageOverflow(), currentTime); //set parameter that activates in visitior collecting mechanism. Which prepares new activeanimationMap for swap.
        updateVisitor.NotifyStoryBoards(); //Inform storyboard about their deactivation due all their scenes animations are finished.
    }

    bool ActiveAnimationsManager::IsCleanupNeededHeuristic(bool garbageOverFlow, const Candera::TimeType& currentTime) {
        if (garbageOverFlow) { //if flag about garbage overflow is active for more than 10 seconds continuously than cleanup is needed.
            if (currentTime - m_mapOverflowDurationCounter > cTenSeconds) {
                return true;
            }
        }
        else {
            m_mapOverflowDurationCounter = currentTime;
        }
        return false;
    }

    Candera::VwXml::StoryBoard* ActiveAnimationsManager::SceneToStoryBoard(Candera::VwXml::Scene* scene)
    {
        if (nullptr != scene) {
            Candera::AbstractNodePointer nodePtr = scene->GetNode();
            if (nodePtr.IsValid()) {
                nodePtr = nodePtr.GetParent(); //should be node of Storyboard. Scene always should be directly under node with storyboard.
                if (nodePtr.IsValid()) {
                    Candera::Behavior* bhv = Candera::Behavior::GetFirstBehavior(nodePtr);
                    while (nullptr != bhv) {
                        Candera::VwXml::StoryBoard* storyboard = Candera::Dynamic_Cast<Candera::VwXml::StoryBoard*>(bhv);
                        if (nullptr != storyboard) {
                            return storyboard;
                        }
                        bhv = bhv->GetNextBehavior();
                    }
                }
            }
        }
        return nullptr;
    }

    void ActiveAnimationsManager::TimeUpdater(const Candera::TimeType currentTime, const Candera::TimeType passedMs)
    {
        UpdateAnimations(currentTime, passedMs); //Update states of all actively running animations, based on current application time.
        StateMachineBasedAnimationSupport::GetInstance().UpdateDeactivateAnimation();//Scene deactivate animations (set of storyboard) is collected as animation object which need to be updated every frame when deactivation scene animataion set is running
                                                                                     //to determine is animation complete and we can order state machine change to reach next scene.
    }

}
