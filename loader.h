#ifndef LOADER
#define LOADER

#include <Eigen/Core>

void load_model(
    std::string model, 
    Eigen::MatrixXd& V,
    Eigen::MatrixXd& uv,
    Eigen::MatrixXi& F,
    Eigen::MatrixXd& P,
    Eigen::VectorXi& R,
    Eigen::VectorXi& T
);

void load_model_with_seam(
    const std::string model,
    Eigen::MatrixXd& V,
    Eigen::MatrixXi& F,
    Eigen::MatrixXd& polygon,
    Eigen::VectorXi& bd
);


void load_matching_info(
    std::string fname,
    std::pair<int,int>& match
);

//jiaran
void load_in(const std::string model,
             Eigen::MatrixXd& V,
             Eigen::MatrixXi& F,
             Eigen::MatrixXd& polygon,
             Eigen::VectorXi& bd,
             Eigen::VectorXi& R);

void load_in(const std::string model,
             Eigen::MatrixXd& V,
             Eigen::MatrixXi& F,
             Eigen::VectorXi& R,
             Eigen::VectorXi& bd,
             Eigen::MatrixXd& polygon,
             Eigen::MatrixXi& EE);

//jiaran end
#endif