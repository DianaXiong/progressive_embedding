#include "loader.h"
#include <igl/boundary_loop.h>
#include <igl/readOBJ.h>
#include "shor.h"

void load_model(
    std::string model, 
    Eigen::MatrixXd& V,
    Eigen::MatrixXd& uv,
    Eigen::MatrixXi& F,
    Eigen::MatrixXd& P,
    Eigen::VectorXi& R,
    Eigen::VectorXi& T
){
    Eigen::MatrixXd CN;
    Eigen::MatrixXi Fuv,FN;
    igl::readOBJ(model,V,uv,CN,F,Fuv,FN);

    // copy face from uv
    std::vector<std::vector<int>> D1,D2;
    igl::boundary_loop(F  ,D1);    
    igl::boundary_loop(Fuv,D2);
    std::cout<<"model size: #F("<<F.rows()<<"), #V("<<V.rows()<<")"<<std::endl;
    std::cout<<"source #BD("<<D1.size()<<")"<<std::endl;
    std::cout<<"target #BD("<<D2.size()<<")"<<std::endl;
    if(Fuv.rows()>0){
      Eigen::MatrixXd nV(uv.rows(),3);
      for(int i=0;i<F.rows();i++){
          nV.row(Fuv(i,0)) << V.row(F(i,0));
          nV.row(Fuv(i,1)) << V.row(F(i,1));
          nV.row(Fuv(i,2)) << V.row(F(i,2));
      }
      F = Fuv;
      V = nV;
      //fill_in_holes(V,F);
      auto D = D2[0];
      P.resize(D.size(),2);
      T.resize(P.rows());
      set_rotation_index(uv,Fuv,R);
      for(int i=0;i<P.rows();i++){
          P.row(i)<<uv.row(D[i]);
          T(i) = D[i];
      }
    }
}

void load_model_with_seam(
    const std::string model,
    Eigen::MatrixXd& V,
    Eigen::MatrixXi& F,
    Eigen::MatrixXd& polygon,
    Eigen::VectorXi& bd
){
    Eigen::MatrixXd uv,CN;
    Eigen::MatrixXi Fuv,FN;
    igl::readOBJ(model,V,uv,CN,F,Fuv,FN);
    if(uv.rows()!=0){
        Eigen::MatrixXd nV(uv.rows(),3);
        for(int i=0;i<F.rows();i++){
            nV.row(Fuv(i,0)) << V.row(F(i,0));
            nV.row(Fuv(i,1)) << V.row(F(i,1));
            nV.row(Fuv(i,2)) << V.row(F(i,2));
        }
        F = Fuv;
        V = nV;
    }
    igl::boundary_loop(F,bd);
    if(uv.rows()!=0)
        igl::slice(uv,bd,1,polygon);
}

void load_matching_info(
    std::string fname,
    std::pair<int,int>& match
){
    // Open file, and check for error
    FILE * obj_file = fopen(fname.c_str(),"r");
    if(NULL==obj_file){
        fprintf(stderr,"IOError: %s could not be opened...\n",
                fname.c_str());
        return;
    }
    #define LINE_MAX_S 2048
    char line[LINE_MAX_S];
    int line_no = 1;
    while (fgets(line, LINE_MAX_S, obj_file) != NULL){
        char type[LINE_MAX_S];
        // Read first word containing type
        if(sscanf(line, "%s",type) == 1){
            char * l = &line[strlen(type)];
            if(strlen(type) >= 1){
                if (type[0] == 'b'){ // index of point constraints
                    int x,y;
                    sscanf(l,"%d%d\n",&x,&y);
                    match = std::make_pair(x,y);
                }
            }
        }
    }
}

//jiaran
void load_in(const std::string model,
             Eigen::MatrixXd& V,
             Eigen::MatrixXi& F,
             Eigen::MatrixXd& polygon,
             Eigen::VectorXi& bd,
             Eigen::VectorXi& R)
{
    Eigen::MatrixXi EE;
    load_in(model,V,F,R,bd,polygon,EE);
}

void load_in(const std::string model,
             Eigen::MatrixXd& V,
             Eigen::MatrixXi& F,
             Eigen::VectorXi& R,
             Eigen::VectorXi& bd,
             Eigen::MatrixXd& polygon,
             Eigen::MatrixXi& EE)
{
    Eigen::MatrixXd uv,CN;
    Eigen::MatrixXi Fuv,FN;
    igl::readOBJ(model,V,uv,CN,F,Fuv,FN);
    
    if(uv.rows()!=0)
    {
        Eigen::MatrixXd nV(uv.rows(),3);
        
        for(int i=0;i<F.rows();i++)
        {
            nV.row(Fuv(i,0)) << V.row(F(i,0));
            nV.row(Fuv(i,1)) << V.row(F(i,1));
            nV.row(Fuv(i,2)) << V.row(F(i,2));
        }
        
        F = Fuv;
        V = nV;
    }
    
    igl::boundary_loop(F,bd);
    
    if(uv.rows()!=0)
        igl::slice(uv,bd,1,polygon);
    
    FILE * file = fopen(model.c_str(),"r");
    
#define LINE_MAX 2048
    char line[LINE_MAX];
    
    Eigen::VectorXi RI;
    RI.setZero(V.rows());
    EE.setZero();
    
    while (fgets(line, LINE_MAX, file) != NULL)
    {
        char type[LINE_MAX];
        
        if(sscanf(line, "%s",type) == 1)
        {
            char * l = &line[strlen(type)];
            
            if(strlen(type) >= 1)
            {
                if (type[0] == 'r')
                {
                    int x[2];
                    int count = sscanf(l,"%d %d\n",&x[0],&x[1]);
                    RI(x[0]-1) = x[1];
                }
                else if(type[0] == 'e')
                {
                    int x[6];
                    int count = sscanf(l,"%d %d %d %d %d %d\n",&x[0],&x[1],&x[2],&x[3],&x[4],&x[5]);
                    EE.conservativeResize(EE.rows()+1,6);
                    EE.bottomRows(1)<<x[0]-1,x[1]-1,x[2]-1,x[3]-1,x[4],x[5];
                }
            }
        }
        
        //else: ignore empty line
    }
    
    fclose(file);
    
    R.setZero(bd.rows());
    for (int i=0; i<bd.rows(); i++)
        R(i) = RI(bd(i));
}
//jiaran end
