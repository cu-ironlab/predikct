// includes
#include "predikct/user_prediction_node.h"
#include "predikct/motion_candidate_node.h"
#include "predikct/user_model.h"
#include <random>
#include <math.h>

namespace predikct
{

// forward declares
class MotionCandidateNode;

UserPredictionNode::UserPredictionNode(TreeNode* parent, RobotModel* robot_model, boost::shared_ptr<MotionState> state, int tree_depth, TreeSpec* tree_spec, RewardCalculator* reward_calculator, 
    UserModel* user_model, std::vector<double>* current_velocity) 
    : TreeNode(parent, robot_model, state, tree_depth, tree_spec, reward_calculator, user_model)
{
    children_generated_ = false;
    current_velocity_.clear();
    for(int i = 0; i < current_velocity->size(); ++i)
    {
        current_velocity_.push_back((*current_velocity)[i]);
    }
}

std::vector<boost::shared_ptr<TreeNode>> UserPredictionNode::GenerateChildren()
{
    children_.clear();
    velocity_primitives_.clear();
    sampled_primitives_.clear();
    sampled_probabilities_.clear();

    std::default_random_engine generator;
    //Create normal distributions for each velocity primitive
    // with mean of current velocity value and standard deviation of 1
    std::vector<std::normal_distribution<double>> velocity_distributions;
    for(int i = 0; i < current_velocity_.size(); i++)
    {
        velocity_distributions.push_back(std::normal_distribution<double>(current_velocity_[i], 0.1));
    }

    double current_linear_norm = sqrt(pow(current_velocity_[0], 2) + pow(current_velocity_[1], 2) + pow(current_velocity_[2], 2));
    double current_angular_norm = sqrt(pow(current_velocity_[3], 2) + pow(current_velocity_[4], 2) + pow(current_velocity_[5], 2));
    //Generate number of velocity primitives according to tree specification
    for(int i = 0; i < tree_spec_->velocity_primitive_set_size; i++)
    {
        //Create new velocity primitive
        std::vector<double> velocity_primitive;
        for(int j = 0; j < velocity_distributions.size(); j++)
        {
            if(i == 0)
            {
                velocity_primitive.push_back(current_velocity_[j]);
            }
            else {
                velocity_primitive.push_back(velocity_distributions[j](generator));
            }
        }
        double linear_magnitude = sqrt(pow(velocity_primitive[0], 2) + pow(velocity_primitive[1], 2) + pow(velocity_primitive[2], 2));
        double angular_magnitude = sqrt(pow(velocity_primitive[3], 2) + pow(velocity_primitive[4], 2) + pow(velocity_primitive[5], 2));
        if(linear_magnitude != 0.0)
        {
            velocity_primitive[0] = current_linear_norm * (velocity_primitive[0] / linear_magnitude);
            velocity_primitive[1] = current_linear_norm * (velocity_primitive[1] / linear_magnitude);
            velocity_primitive[2] = current_linear_norm * (velocity_primitive[2] / linear_magnitude);
        }

        if(angular_magnitude != 0.0)
        {
            velocity_primitive[3] = current_angular_norm * (velocity_primitive[3] / angular_magnitude);
            velocity_primitive[4] = current_angular_norm * (velocity_primitive[4] / angular_magnitude);
            velocity_primitive[5] = current_angular_norm * (velocity_primitive[5] / angular_magnitude);
        }
        velocity_primitives_.push_back(velocity_primitive);
    }

    //Generate number of children according to branching factor
    user_model_->SampleNoReplacement(robot_model_, state_, &current_velocity_, &velocity_primitives_, tree_spec_->time_window,
        tree_spec_->user_prediction_branching_factor, &sampled_primitives_, &sampled_probabilities_);
    for(int i = 0; i < tree_spec_->user_prediction_branching_factor; i++)
    {
        children_.push_back(boost::shared_ptr<TreeNode>( new MotionCandidateNode(this, robot_model_, state_, 
            node_depth_+1, tree_spec_, reward_calc_, user_model_, &(sampled_primitives_[i]))));
    }

    children_generated_ = true;
    return children_;
}

double UserPredictionNode::CalculateReward()
{
    if(!children_generated_)
    {
        GenerateChildren();
    }

    double reward = 0.0;
    //Sum over all children nodes
    for(int i = 0; i < sampled_primitives_.size(); i++)
    {
        double primitive_score = sampled_probabilities_[i] * children_[i]->GetReward();
        reward += primitive_score;
    }

    //Multiply by discount factor (gamma)
    reward *= tree_spec_->temporal_discount;
    
    reward_ = reward;
    reward_calculated_ = true;
    return reward_;
}

}