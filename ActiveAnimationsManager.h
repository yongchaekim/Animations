//########################################################################
// (C) Candera GmbH
// All rights reserved.
// -----------------------------------------------------
// This document contains proprietary information belonging to
// Candera GmbH.
// Passing on and copying of this document, use and communication
// of its contents is not permitted without prior written authorization.
//########################################################################

#ifndef ActiveAnimationsManager_h__
#define ActiveAnimationsManager_h__
#include <Candera/System/Container/Map.h>
#include <Candera/System/Container/Vector.h>

#include <Application/Behaviors/VwXml/Scene.h>
#include <Application/Behaviors/VwXml/StoryBoard.h>
#include <queue>
#include <mutex>
#include <functional>


namespace App {

    class UpdateAnimationsVisitor;

    class ActiveAnimationsManager {

    public:
        typedef Candera::VwXml::StoryBoard* StoryBoardPtr;

        static ActiveAnimationsManager& GetInstance();
        //ActiveAnimationsManager destructor. Deregister Manager from UpdateSystem.
        ~ActiveAnimationsManager();
        bool RegisterAnimation(Candera::VwXml::Scene* scene);
        //Unregister animation from active animations map. Called only on application close( scene destructor) due in regular program execution there is no need of unregister. ActiveAnimation Manager, unregister automatically scene when finished by seting its pointer in vector to null.
        void UnregisterAnimation(Candera::VwXml::Scene* scene);
        //Unregister animation from active animations map. Called only on application close( scene destructor) due in regular program execution there is no need of unregister. ActiveAnimation Manager, unregister automatically storyboard which have 0 running scene animations.
        void UnregisterAnimation(Candera::VwXml::StoryBoard* storyBoard);
        //Based on StoryBoard parameter alows to determine is any Scene under this stroyboard is running animation.
        //@return true if there is any active scene from storyboard. false if story board not exists in map or its map value pointing to null.
        bool IsAnimationRunning(const StoryBoardPtr storyBoard) const; 

        // Enqueue an operation for processing
        void EnqueueOperation(const std::function<void()>& operation);

    private:
        //ActiveAnimationManager constructor. Due it should be a singleton, constructor have to be private. Register callback to UpdateSystem.
        ActiveAnimationsManager();
        ActiveAnimationsManager(ActiveAnimationsManager& other) = delete;
        void operator=(const ActiveAnimationsManager&) = delete;

        typedef Candera::Internal::Vector<Candera::VwXml::Scene*> ScenesVec;
        typedef Candera::Internal::Map<StoryBoardPtr, ScenesVec> ActiveAnimationsMap;

        //Method used to update ActiveAnimationsManager states on every frame. Called only from TimeUpdater(). Animation update in loop logic.
        void UpdateAnimations(const Candera::TimeType currentTime, const Candera::TimeType passedMs);

        //Helper method to get StoryBoard based on Scene input. DUe scene registers into ActiveAnimationManager, but manager have to collect them under proper storyboard which define complete animation.
        Candera::VwXml::StoryBoard* SceneToStoryBoard(Candera::VwXml::Scene* scene);

        //Method to check is map of active animation need to be recreated due too many finished animations objects (garbage).
        //@return true if @garbageOverFlow flag is set to true for more than 10 seconds.
        bool IsCleanupNeededHeuristic(bool garbageOverFlow, const Candera::TimeType& currentTime);

        /*Method registered in Update Sysytem (callback). With every frame updates states of all undergoing animations.
         @param currentTime provides current system time to calculate progress of animations.
         @param  passedMs  provides how many miliseconds passed since previous System update ( how long frame took). Unused parameter in animation just passed in case needed in future.
        */
        void TimeUpdater(const Candera::TimeType currentTime, const Candera::TimeType passedMs);

        // Process the centralized queue
        void ProcessQueue();

        ActiveAnimationsMap m_activeAnimationsMap;
        bool m_cleanupOnNextCall;
        Candera::TimeType m_mapOverflowDurationCounter;

        std::queue<std::function<void()>> m_operationQueue;
        std::mutex m_queueMutex;

        friend class UpdateAnimationsVisitor;

        
    };

}
#endif
