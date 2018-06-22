/* Author: Zachary Kingston */

#ifndef ROBOWFLEX_PLANNER_
#define ROBOWFLEX_PLANNER_

namespace robowflex
{
    ROBOWFLEX_CLASS_FORWARD(Planner);

    /** \brief An abstract interface to a motion planning algorithm.
     */
    class Planner
    {
    public:
        /** \brief Constructor.
         *  Takes in a \a robot description and an optional namespace \a name.
         *  If \a name is specified, planner parameters are namespaced under the namespace of \a robot.
         *  \param[in] robot The robot to plan for.
         *  \param[in] name Optional namespace for planner.
         */
        Planner(const RobotPtr &robot, const std::string &name = "")
          : robot_(robot), handler_(robot_->getHandler(), name), name_(name)
        {
        }

        // non-copyable
        Planner(Planner const &) = delete;
        void operator=(Planner const &) = delete;

        /** \brief Plan a motion given a \a request and a \a scene.
         *  A virtual method that must be implemented by any robowflex::Planner.
         *  \param[in] scene A planning scene for the same \a robot_ to compute the plan in.
         *  \param[in] request The motion planning request to solve.
         *  \return The motion planning response generated by the planner.
         */
        virtual planning_interface::MotionPlanResponse
        plan(const SceneConstPtr &scene, const planning_interface::MotionPlanRequest &request) = 0;

        /** \brief Return all planner configurations offered by this planner.
         *  Any of the configurations returned can be set as the planner for a motion planning query sent to
         *  plan().
         *  \return A vector of strings of planner configuration names.
         */
        virtual const std::vector<std::string> getPlannerConfigs() const = 0;

        /** \brief Return the robot for this planner.
         */
        const RobotPtr getRobot() const
        {
            return robot_;
        }

    protected:
        RobotPtr robot_;          ///< The robot to plan for.
        IO::Handler handler_;     ///< The parameter handler for the planner.
        const std::string name_;  ///< Namespace for the planner.
    };

    /** \brief A thread pool of planners \a P to service requests in a multi-threaded environment
     *  simultaneously.
     *  \tparam P The robowflex::Planner to pool.
     */
    template <typename P>
    class PoolPlanner : public Planner
    {
    public:
        /** \brief Constructor.
         *  Takes in a \a robot description and an optional namespace \a name.
         *  If \a name is specified, planner parameters are namespaced under the namespace of \a robot.
         *  \param[in] robot The robot to plan for.
         *  \param[in] n The number of threads to use. By default uses maximum available on the machine.
         *  \param[in] name Optional namespace for planner.
         */
        PoolPlanner(const RobotPtr &robot, unsigned int n = std::thread::hardware_concurrency(),
                    const std::string &name = "")
          : Planner(robot, name), pool_(n)
        {
        }

        // non-copyable
        PoolPlanner<P>(PoolPlanner<P> const &) = delete;
        void operator=(PoolPlanner<P> const &) = delete;

        /** \brief Initialize the planner pool.
         *  Forwards template arguments \a Args to the initializer of the templated planner \a P. Assumes that
         *  the constructor of the planner takes \a robot_ and \a name_.
         *  \param[in] args Arguments to initializer of planner \a P.
         *  \tparam Args Argument types to initializer of planner \a P.
         */
        template <typename... Args>
        bool initialize(Args &&... args)
        {
            for (unsigned int i = 0; i < pool_.getThreadCount(); ++i)
            {
                auto planner = std::make_shared<P>(robot_, name_);

                planner->initialize(std::forward<Args>(args)...);
                planners_.emplace(std::move(planner));
            }
        }

        /** \brief Plan a motion given a \a request and a \a scene.
         *  Forwards the planning request onto the thread pool to be executed. Blocks until complete and
         *  returns result.
         *  \param[in] scene A planning scene for the same \a robot_ to compute the plan in.
         *  \param[in] request The motion planning request to solve.
         *  \return The motion planning response generated by the planner.
         */
        planning_interface::MotionPlanResponse
        plan(const SceneConstPtr &scene, const planning_interface::MotionPlanRequest &request) override
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !planners_.empty(); });

            auto planner = planners_.front();
            planners_.pop();

            lock.unlock();

            auto result = pool_.process([&] { return planner->plan(scene, request); });

            lock.lock();
            planners_.emplace(planner);
            cv_.notify_one();

            return result;
        }

        const std::vector<std::string> getPlannerConfigs() const override
        {
            return planners_.front()->getPlannerConfigs();
        }

    private:
        Pool<planning_interface::MotionPlanResponse> pool_;  ///< Thread pool

        std::queue<PlannerPtr> planners_;  ///< Motion planners
        std::mutex mutex_;                 ///< Planner mutex
        std::condition_variable cv_;       ///< Planner condition variable
    };

    ROBOWFLEX_CLASS_FORWARD(PipelinePlanner);

    /** \brief A motion planner that uses the \a MoveIt! planning pipeline to load a planner plugin.
     */
    class PipelinePlanner : public Planner
    {
    public:
        PipelinePlanner(const RobotPtr &robot, const std::string &name = "") : Planner(robot, name)
        {
        }

        // non-copyable
        PipelinePlanner(PipelinePlanner const &) = delete;
        void operator=(PipelinePlanner const &) = delete;

        /** \brief Plan a motion given a \a request and a \a scene.
         *  Uses the planning pipeline's generatePlan() method, which goes through planning adapters.
         *  \param[in] scene A planning scene for the same \a robot_ to compute the plan in.
         *  \param[in] request The motion planning request to solve.
         *  \return The motion planning response generated by the planner.
         */
        planning_interface::MotionPlanResponse
        plan(const SceneConstPtr &scene, const planning_interface::MotionPlanRequest &request) override;

    protected:
        planning_pipeline::PlanningPipelinePtr pipeline_;  ///< Loaded planning pipeline plugin.
    };

    ROBOWFLEX_CLASS_FORWARD(MotionRequestBuilder);

    /** \brief A helper class to build motion planning requests for a robowflex::Planner
     */
    class MotionRequestBuilder
    {
    public:
        /** \brief Constructor.
         *  \param[in] planner The motion planner to build a request for.
         *  \param[in] group_name The motion planning group to build the request for.
         */
        MotionRequestBuilder(const PlannerConstPtr &planner, const std::string &group_name);

        /** \brief Sets workspace bounds of the planning request.
         *  \param[in] wp The workspace parameters to use.
         */
        void setWorkspaceBounds(const moveit_msgs::WorkspaceParameters &wp);

        /** \brief Set the start configuration from a vector \a joints.
         *  All joints are assumed to be specified and in the default order.
         *  \param[in] joints The values of the joints to set.
         */
        void setStartConfiguration(const std::vector<double> &joints);

        /** \brief Set the goal configuration from a vector \a joints.
         *  All joints are assumed to be specified and in the default order.
         *  \param[in] joints The values of the joints to set.
         */
        void setGoalConfiguration(const std::vector<double> &joints);

        /** \brief Set a goal region for an end-effector \a ee_name.
         *  Sets the position constraint from \a geometry at a pose \a pose, and the orientation constraint
         *  from \a orientation and XYZ Euler angle tolerances \a tolerances.
         *  \param[in] ee_name The name of the end-effector link.
         *  \param[in] base_name The frame of pose and orientation.
         *  \param[in] pose The pose of \a geometry in \a base_frame.
         *  \param[in] geometry The geometry describing the position constraint.
         *  \param[in] orientation The desired orientation.
         *  \param[in] tolerances XYZ Euler angle tolerances about orientation.
         */
        void setGoalRegion(const std::string &ee_name, const std::string &base_name,
                           const Eigen::Affine3d &pose, const Geometry &geometry,
                           const Eigen::Quaterniond &orientation, const Eigen::Vector3d &tolerances);

        /** \brief Set a pose constraint on the path.
         *  Sets the position constraint from \a geometry at a pose \a pose, and the orientation constraint
         *  from \a orientation and XYZ Euler angle tolerances \a tolerances.
         *  \param[in] ee_name The name of the end-effector link.
         *  \param[in] base_name The frame of pose and orientation.
         *  \param[in] pose The pose of \a geometry in \a base_frame.
         *  \param[in] geometry The geometry describing the position constraint.
         *  \param[in] orientation The desired orientation.
         *  \param[in] tolerances XYZ Euler angle tolerances about orientation.
         */
        void addPathPoseConstraint(const std::string &ee_name, const std::string &base_name,
                                   const Eigen::Affine3d &pose, const Geometry &geometry,
                                   const Eigen::Quaterniond &orientation, const Eigen::Vector3d &tolerances);

        /** \brief Set a position constraint on the path.
         *  Sets the position constraint from \a geometry at a pose \a pose.
         *  \param[in] ee_name The name of the end-effector link.
         *  \param[in] base_name The frame of pose and orientation.
         *  \param[in] pose The pose of \a geometry in \a base_frame.
         *  \param[in] geometry The geometry describing the position constraint.
         */
        void addPathPositionConstraint(const std::string &ee_name, const std::string &base_name,
                                       const Eigen::Affine3d &pose, const Geometry &geometry);

        /** \brief Set an orientation constraint on the path.
         *  Sets the orientation constraint from \a orientation and XYZ Euler angle tolerances \a tolerances.
         *  \param[in] ee_name The name of the end-effector link.
         *  \param[in] base_name The frame of pose and orientation.
         *  \param[in] orientation The desired orientation.
         *  \param[in] tolerances XYZ Euler angle tolerances about orientation.
         */
        void addPathOrientationConstraint(const std::string &ee_name, const std::string &base_name,
                                          const Eigen::Quaterniond &orientation,
                                          const Eigen::Vector3d &tolerances);

        /** \brief Set the allowed planning time in the request.
         *  \param[in] allowed_planning_time The allowed planning time.
         */
        void setAllowedPlanningTime(double allowed_planning_time);

        /** \brief Get a reference to the currently built motion planning request.
         *  \return The motion planning request.
         */
        const planning_interface::MotionPlanRequest &getRequest() const;

        /** \brief Get a reference to the current path constraints on the motion planning request.
         *  \return The motion planning request.
         */
        moveit_msgs::Constraints &getPathConstraints();

        /** \brief Serialize the motion planning request to a YAML file \a file.
         *  \param[in] file The name of the file to serialize the request to.
         *  \return True on success, false on failure.
         */
        bool toYAMLFile(const std::string &file);

        /** \brief Load a planning request from a YAML file \a file.
         *  \param[in] file The name of the file to load the request from.
         *  \return True on success, false on failure.
         */
        bool fromYAMLFile(const std::string &file);

        /** \brief Set the planning configuration to use for the motion planning request.
         *  Attempts to match \a requested_config with the planner configuration offered by \a planner_
         *  that is the shortest configuration that contains \a requested_config as a substring. For example,
         *  specifying `RRTConnect` will match `RRTConnectkConfigDefault`, and specifying `RRT` will match
         *  `RRTkConfigDefault` and not `RRTConnectkConfigDefault`.
         *  \param[in] requested_config The planner config to find and use.
         *  \return True if the \a requested_config is found, false otherwise.
         */
        bool setConfig(const std::string &requested_config);

    private:
        const PlannerConstPtr planner_;            ///< The planner to build the request for.
        const RobotConstPtr robot_;                ///< The robot to build the request for (from \a planner_)
        const std::string group_name_;             ///< The group to plan for.
        const robot_model::JointModelGroup *jmg_;  ///< The joint model group of the robot (from \a
                                                   ///< group_name_)

        planning_interface::MotionPlanRequest request_;  ///< The build request.

        static const std::vector<std::string> DEFAULT_CONFIGS;  ///< Default planner configurations to use
    };

    /** \brief TODO: Document */
    std::vector<double> getFinalJointPositions(planning_interface::MotionPlanResponse response);

    namespace OMPL
    {
        /** \brief Settings descriptor for settings provided by the default \a MoveIt! OMPL planning pipeline.
         */
        class Settings
        {
        public:
            /** \brief Constructor.
             *  Initialized here so an empty class can be used as default arguments in a function.
             */
            Settings()
              : max_goal_samples(10)
              , max_goal_sampling_attempts(1000)
              , max_planning_threads(4)
              , max_solution_segment_length(0.0)
              , max_state_sampling_attempts(4)
              , minimum_waypoint_count(10)
              , simplify_solutions(true)
              , use_constraints_approximations(false)
              , display_random_valid_states(false)
              , link_for_exploration_tree("")
              , maximum_waypoint_distance(0.0)
            {
            }

            int max_goal_samples;                   ///< Maximum number of successful goal samples to keep.
            int max_goal_sampling_attempts;         ///< Maximum number of attempts to sample a goal.
            int max_planning_threads;               ///< Maximum number of threads used to service a request.
            double max_solution_segment_length;     ///< Maximum solution segment length.
            int max_state_sampling_attempts;        ///< Maximum number of attempts to sample a new state.
            int minimum_waypoint_count;             ///< Minimum number of waypoints in output path.
            bool simplify_solutions;                ///< Whether or not planner should simplify solutions.
            bool use_constraints_approximations;    ///< Absolute silliness.
            bool display_random_valid_states;       ///< N/A, defunct.
            std::string link_for_exploration_tree;  ///< N/A, defunct.
            double maximum_waypoint_distance;       ///< Maximum distance between waypoints in path.

            /** \brief Sets member variables on the parameter server using \a handler.
             */
            void setParam(IO::Handler &handler) const;
        };

        ROBOWFLEX_CLASS_FORWARD(OMPLPipelinePlanner);

        /** \brief A robowflex::PipelinePlanner that uses the \a MoveIt! default OMPL planning pipeline.
         */
        class OMPLPipelinePlanner : public PipelinePlanner
        {
        public:
            OMPLPipelinePlanner(const RobotPtr &robot, const std::string &name = "");

            // non-copyable
            OMPLPipelinePlanner(OMPLPipelinePlanner const &) = delete;
            void operator=(OMPLPipelinePlanner const &) = delete;

            /** \brief Initialize planning pipeline.
             *  Loads OMPL planning plugin \a plugin with the planning adapters \a adapters. Parameters are
             *  set on the parameter server from \a settings and planning configurations are loaded from the
             *  YAML file \a config_file.
             *  \param[in] config_file A YAML file containing OMPL planner configurations.
             *  \param[in] settings Settings to set on the parameter server.
             *  \param[in] plugin Planning plugin to load.
             *  \param[in] adapters Planning adapters to load.
             *  \return True upon success, false on failure.
             */
            bool initialize(const std::string &config_file = "", const Settings settings = Settings(),
                            const std::string &plugin = DEFAULT_PLUGIN,
                            const std::vector<std::string> &adapters = DEFAULT_ADAPTERS);

            const std::vector<std::string> getPlannerConfigs() const override;

        protected:
            static const std::string DEFAULT_PLUGIN;                 ///< The default OMPL plugin.
            static const std::vector<std::string> DEFAULT_ADAPTERS;  ///< The default planning adapters.

        private:
            std::vector<std::string> configs_;  ///< Planning configurations loaded from \a config_file in
                                                ///< initialize()
        };
    }  // namespace OMPL
}  // namespace robowflex

#endif
