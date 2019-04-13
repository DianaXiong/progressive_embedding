#include "progressive_embedding.h"
#include <Eigen/Dense>
#include "local_operation.h"
#include "local_smooth/energy.h"
#include <queue>
#include <igl/triangle_triangle_adjacency.h>
#include <igl/opengl/glfw/Viewer.h>
#include <igl/copyleft/cgal/orient2D.h>
#include "plot.h"

#include <igl/remove_unreferenced.h>
#include <igl/opengl/glfw/imgui/ImGuiMenu.h>
#include <igl/opengl/glfw/imgui/ImGuiHelpers.h>
#include "local_smooth/local_smooth.h"
#include <math.h>
#include <igl/slim.h>
#include <igl/writeOFF.h>
#include <igl/Timer.h>
#include <igl/linprog.h>
#include <limits>
#include "local_smooth/auto_grad.hpp"
#define LOG

bool is_face_flipped(const Eigen::Matrix<double,3,2>& T){
  double a[2] = {T(0,0),T(0,1)};
  double b[2] = {T(1,0),T(1,1)};
  double c[2] = {T(2,0),T(2,1)};
  return igl::copyleft::cgal::orient2D(a,b,c) <= 0;
}

bool is_face_valid(
  const Eigen::Matrix<double,2,3>& G_t,
  const Eigen::Matrix<double,3,2>& T,
  const double threshold
){
  bool flipped = is_face_flipped(T);
  double e = autogen::sd_energy(T,G_t);
  return std::isfinite(e) && (e < threshold) && !flipped;
}



int expand_to_boundary(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F,
  const Eigen::VectorXi& B,
  const Eigen::MatrixXi& dEF_s,
  const int fid,
  Eigen::MatrixXi& local_F
){
  Eigen::MatrixXi dEF = dEF_s;
  std::set<int> N;
  std::deque<int> Q;
  N.clear();
  int level = 0;
  for(int i=0;i<F.rows();i++){
    for(int k=0;k<3;k++){
      int e = F.rows()*((k+2)%3)+i;
      int k_1 = (k+1)%3;
      if(B(F(i,k)) && B(F(i,k_1)))
        dEF(e,1) = -1;
    }
  }
  if(fid != -1){
    Q.push_back(fid);
    N.insert(fid);
    while(!Q.empty()){
      int f = Q.front();
      Q.pop_front();
      for(int i=0;i<3;i++){
        int e = F.rows()*((i+2)%3)+f;
        if(dEF(e,1)!=-1 && N.find(dEF(e,1))==N.end()){
          Q.push_back(dEF(e,1));
          N.insert(dEF(e,1));
        }
      }
    }
  }
  Q.clear();
  local_F.resize(N.size(),3);
  int i=0;
  for(int x: N){    
      local_F.row(i++)<<F.row(x);
  }
  std::cout<<"count interior points for "<<std::endl;
  int my_id = 0;
  int count=0;
  std::set<int> interior;
  for(int i=0;i<local_F.rows();i++){
    for(int k=0;k<3;k++){
      int id = local_F(i,k);
      if(!B(id)){
        my_id = id;
        interior.insert(my_id);
        //std::cout<<my_id<<std::endl;
      }
    }
  }
  std::cout<<"#interior point "<<interior.size()<<std::endl;
  return interior.size()==1?my_id:-1;
}

void get_neighbor_at_level(
    const Eigen::MatrixXd& V,
    const Eigen::MatrixXi& F,
    const Eigen::MatrixXi& dEF,
    int fid,
    int neighbor,
    Eigen::VectorXi& nbs,
    Eigen::MatrixXi& region
){
    //std::cout << "Hello\n";
    std::set<int> N;
    std::deque<std::pair<int,int>> Q;
    N.clear();
    int level = 0;
    if(fid != -1){
        std::pair<int,int> p(fid,0);
        Q.push_back(p);
        N.insert(fid);
        int l = 0;
        while(l<neighbor && !Q.empty()){
            int f = Q.front().first;
            l = Q.front().second;
            if(l == neighbor) break;
            Q.pop_front();
            for(int i=0;i<3;i++){
              int e = F.rows()*((i+2)%3)+f;
              if(dEF(e,1)!=-1 && N.find(dEF(e,1))==N.end()){
                    std::pair<int,int> p(dEF(e,1),l+1);
                    Q.push_back(p);
                    N.insert(dEF(e,1));
              }
            }
        }
    }
    Q.clear();
    region.resize(N.size(),3);
    int i=0;
    nbs.resize(N.size());
    //std::cout<<"neighbor num "<<region.rows()<<std::endl;
    for(int x: N){
        nbs.row(i)<<x; 
        region.row(i++)<<F.row(x);
    }
}

bool is_flipped(const Eigen::MatrixXd& V3d, const Eigen::MatrixXd& V, const Eigen::MatrixXi& F, const std::vector<int>& I){
    // check flip
    int flipped = 0;
    for(int i: I){
        int x = F(i,0);
        int y = F(i,1);
        int z = F(i,2);
        double a[2] = {double(V(x, 0)), double(V(x, 1))};
        double b[2] = {double(V(y, 0)), double(V(y, 1))};
        double c[2] = {double(V(z, 0)), double(V(z, 1))};
        short k = igl::copyleft::cgal::orient2D(a, b, c);
        if(k <= 0) {
            flipped++;
        }
    }
    if(flipped>0)
        return true;
    else
        return false;
}

void find_candidate_positions(
    const Eigen::MatrixXd& uv,
    const Eigen::MatrixXi& ring,
    int v,
    int avoid,
    Eigen::MatrixXd& pos
){
    int pi = 0;
    pos.resize(ring.rows()*10,2);
    for(int i=0;i<ring.rows();i++){
        bool common = false;
        for(int j=0;j<3;j++)
            if(ring(i,j) == avoid){
                common = true;
            }
        if(common) continue;
        for(int k=0;k<3;k++){
            if(ring(i,k) == v){
                int x = ring(i,(k+1)%3);
                int y = ring(i,(k+2)%3);
                pos.row(pi++) << uv.row(x);
                pos.row(pi++) << uv.row(y);
                pos.row(pi++) << (uv.row(x) + uv.row(y))/2;
            }
        }
    }
    pos.conservativeResize(pi,2);

}

double calculate_angle_sum(
    Eigen::MatrixXi& F,
    Eigen::MatrixXd& V,
    int v1,
    int avoid
){
    double angle_sum = 0;
    for(int i=0;i<F.rows();i++){
        bool common = false;
        for(int j=0;j<3;j++)
            if(F(i,j) == avoid){
                common = true;
            }
        if(common) continue;
        for(int k=0;k<3;k++){
            if(F(i,k) == v1){
                Eigen::Vector2d a = V.row(F(i,(k+2)%3));
                Eigen::Vector2d b = V.row(v1);
                Eigen::Vector2d c = V.row(F(i,(k+1)%3));
                auto bc = c - b;
                auto ba = a - b;
                angle_sum += std::acos(bc.dot(ba)/(bc.norm()*ba.norm()));
                break;
            }
        }
    }
    return angle_sum;
}

// find a position for b that maximize the minarea in F
std::pair<bool,double> flip_avoid_line_search(
    const Eigen::MatrixXd& V,
    Eigen::MatrixXd& uv,
    const Eigen::MatrixXi& F,
    int a0,
    int b,
    const Eigen::RowVectorXd& x, // initial guess for b is (x+a)/2
    Eigen::RowVector2d& y,       // output position
    double target_area
){
    double t = 1.0;
    int MAX_IT = 75;
    bool valid = false;
    double maxenergy = std::numeric_limits<double>::max();
    //Eigen::VectorXi dirty;
    std::vector<int> dirty;
    for(int i=0;i<F.rows();i++){
        for(int k=0;k<3;k++){
            if(F(i,k) == b){
                dirty.push_back(i);
            }
        }
    }
    std::vector<double> one_ring_chosen;
    Eigen::Matrix<double,2,3> G_t;
    grad_to_eqtri(target_area,G_t);
    
    for(int it = 0;it<MAX_IT;it++){
        Eigen::RowVector2d pt;
        pt(0) = (1-t)*uv(a0,0)+t*x(0);
        pt(1) = (1-t)*uv(a0,1)+t*x(1);
        t *= 0.8;
        uv.row(b) << pt;
        // calculate the energy of dirty faces
        std::vector<double> one_ring_energy;
        for(int f: dirty){
            Eigen::Matrix<double,3,3> T;
            Eigen::Matrix<double,3,2> Tuv;
            for(int t=0;t<3;t++){
                // make sure vertex v is always the first in T
                Tuv.row(t) << uv.row(F(f,t));
                T.row(t) << V.row(F(f,t));
            }
            one_ring_energy.push_back(autogen::sd_energy(Tuv,G_t));
        }
        
        double max_one_ring_energy = 0.0;
        bool bad_element = false;
        for(double z: one_ring_energy){
            if(!std::isfinite(z))
                bad_element = true;
            if(z > max_one_ring_energy)
                max_one_ring_energy = z;
        }
        if(is_flipped(V,uv,F,dirty) || bad_element) continue;
        if(!std::isfinite(max_one_ring_energy))
            valid = false;
        else 
            valid = true;
        if(max_one_ring_energy < maxenergy){
            maxenergy = max_one_ring_energy;
            one_ring_chosen = one_ring_energy;
            y = pt;
        }
    }
    return std::make_pair(valid,maxenergy);
}

using Action = std::tuple<int,int,Eigen::MatrixXi,std::vector<int>>;

void collect_invalid_elements(
  const Eigen::MatrixXd& V,
  const Eigen::MatrixXi& F,
  const Eigen::MatrixXd& uv,
  const double eps,
  const double avg,
  Eigen::VectorXi& I
){
  // [init Dirichlet energy]
  std::cout<<"init dirichlet energy ... "<<std::endl;
  Eigen::VectorXd Energy;
  std::vector<int> FL(F.rows());
  std::iota(FL.begin(),FL.end(),0);
  compute_dirichlet_energy(avg,uv,F,FL,0,Energy);
  std::cout<<"done"<<std::endl;
  
  // [collect invalid elements]
  std::cout<<"collect invalid elements ..."<<std::endl;
  I.setZero(F.rows());
  count_flipped_element(uv,F,I);
  for(int i=0;i<F.rows();i++){
    if(I(i) || F.row(i).sum()==0) continue;
    if(!std::isfinite(Energy(i))||Energy(i)>eps)
      I(i) = 1;
  }
}

void collapse_invalid_elements(
  const Eigen::MatrixXd& V,
  Eigen::MatrixXi& F,
  Eigen::MatrixXd& uv,
  Eigen::VectorXi& I, //invalid
  const Eigen::VectorXi& B, //boundary
  const double eps,
  const double avg,
  std::vector<Action>& L
){

  // Build adjacency info
  Eigen::VectorXi EMAP,EE;
  Eigen::MatrixXi E,EF,EI;
  Eigen::MatrixXi dEF,dEI,allE;
  igl::edge_flaps(F,E,allE,EMAP,EF,EI,dEF,dEI,EE);
  
  // Use the same size reference shape through out collapsing
  Eigen::Matrix<double,2,3> G;
  grad_to_eqtri(avg,G);

  // Try collapsing an edge 
  // mark the 1-ring faces as `updated` if succ
  auto collapse_if_valid = [&](
    int f, int k, Eigen::VectorXi& updated
  ){
    int e = F.rows()*((k+2)%3)+f;
    int a = F(f,k), b = F(f,(k+1)%3);
    if(B(a) == 1 || B(b) == 1) return false;
    std::vector<int> N; // 1-ring neighbor exclude the collapsed
    if(edge_collapse_is_valid(e,uv.row(b),uv,F,dEF,dEI,EE,allE,N)){
      // unmark the collapsed faces
      I(f)=0;
      I(dEF(e,1))=0;
      Eigen::MatrixXi ring(N.size(),3);
      for(int i=0;i<N.size();i++){
        ring.row(i) << F.row(N[i]);
        updated(N[i]) = 1;
      }
      if(a > b) std::swap(a,b);
      L.push_back(Action(b,a,ring,N));
      collapse_edge(e,uv.row(a),uv,F,dEF,dEI,EE,allE);
      return true;
    }
    return false;
  };

  int num_invalid = I.sum();
  const int MAX_LAYER = 5;
  int layer = 0;
  while(num_invalid!=0){
    bool do_collapse = false;
    Eigen::VectorXi updated(F.rows());
    updated.setZero();
    for(int f=0;f<I.rows();f++){
      if(I(f)==0) continue;
      for(int k=0;k<3;k++){
        do_collapse = (do_collapse || collapse_if_valid(f,k,updated));
      }
    }
    // check and mark invalid elements
    // for updated elements
    for(int i=0;i<I.rows();i++){
      if(updated(i)){
        Eigen::Matrix<double,3,2> T;
        T<<uv.row(F(i,0)),uv.row(F(i,1)),uv.row(F(i,2));
        I(i) = !is_face_valid(G,T,eps);
      }
    }
    num_invalid = I.sum();
    std::cout<<"invalid size "<<num_invalid<<std::endl;

    // expand to 1-ring neighbor if all collapse failed
    layer = do_collapse ? 0 : layer+1;
    
    if(layer == MAX_LAYER){
      std::cout<<"move to barycenter"<<std::endl;
      int n = 0;
      for(int i=0;i<I.rows();i++){
        if(I(i) == 0) continue;
        std::cout<<n++<<"/"<<I.sum()<<std::endl;
        Eigen::MatrixXi local_F;
        int interior_id=expand_to_boundary(V,F,B,dEF,i,local_F);
        //exit(0);
        if(interior_id==-1) continue;
        //plot_mesh(vr,uv,local_F,{});
        Eigen::RowVector2d the_center;
        the_center.setZero();
        Eigen::VectorXi local_bd;
        igl::boundary_loop(local_F,local_bd);
        for(int i=0;i<local_bd.rows();i++){
          the_center += uv.row(local_bd(i));
        }
        the_center /= local_bd.rows();
        uv.row(interior_id) << the_center;
        std::cout<<"move "<<interior_id<<std::endl;
        std::cout<<"to "<<the_center<<std::endl;
        collect_invalid_elements(V,F,uv,eps,avg,I);
        break;
      }
    }

        // also collapse its level-k neighbors
        std::set<int> S_aux;
        Eigen::MatrixXi region;
        for(int i=0;layer!=0 && i<I.rows();i++){
            if(I(i)==0) continue;
            Eigen::VectorXi nbs;
            get_neighbor_at_level(uv,F,dEF,i,layer,nbs,region);
            //S_aux.insert(S[i]);
            for(int j=0;j<nbs.rows();j++)
                S_aux.insert(nbs(j));   
        }
        for(int s: S_aux){
            int count = 0;
            for(int k=0;k<3;k++){
                if(B(F(s,k))==1)
                //if(bdr_s.find(F(s,k)) != bdr_s.end()){
                    count++;
            }
            if(count == 2)
                continue;
            I(s) = 1;
        }
    }
    std::cout<<"L.size() is "<<L.size()<<std::endl;
}
bool insert_vertex_back(
  const std::vector<Action>& L,
  const Eigen::VectorXi& B,
  const Eigen::MatrixXd& V,
  Eigen::MatrixXi& F,
  Eigen::MatrixXd& uv,
  double total_area
){
  // for every action in the list
    igl::Timer timer;
    timer.start();
    bool rollback = false;
    int degree = 2;
    double total_time = 0.0;
    int ii = 0;
    #define SHORTCUT
    #ifdef SHORTCUT
    // deserialize
    std::string serial_name = "carter100";
    igl::deserialize(ii,"ii",serial_name);
    igl::deserialize(uv,"uv",serial_name);
    igl::deserialize(F,"F",serial_name);
    #endif
    std::cout<<ii<<std::endl;
    for(;ii<L.size();ii++){
        double time1 = timer.getElapsedTime();
        std::cout<<"rollback "<<ii<<"/"<<L.size()<<std::endl;
        std::cout<<std::get<0>(L[ii])<<" <-> "<<std::get<1>(L[ii])<<std::endl;
        auto a = L[ii];
        Eigen::MatrixXi ring = std::get<2>(a);
        std::vector<int> nbs = std::get<3>(a);
        // position candidates
        bool found = false;
        Eigen::RowVectorXd pos(2);
        double minarea = std::numeric_limits<double>::min();
        double maxenergy = std::numeric_limits<double>::max();
        Eigen::MatrixXd cd;
        int v0 = std::get<1>(a);
        int v1 = std::get<0>(a);
        auto uv_store = uv;
        uv.row(v1) = uv.row(v0);
        auto F_store = F;
        for(int j=0;j<nbs.size();j++)
            F.row(nbs[j])<<ring.row(j);
        double angle_sum_of_v0 = calculate_angle_sum(ring,uv,v0,v1);
        if(angle_sum_of_v0 < igl::PI)
            std::swap(v0,v1);
        find_candidate_positions(uv,ring,v1,v0,cd); 
        // update average area
        Eigen::VectorXd area;
        double avg = 0.0;
        igl::doublearea(uv,F,area);
        int ctf=0;
        for(int k=0;k<area.rows();k++){
            if(!isnan(area(k)) && !isinf(area(k))){
                avg += area(k);
                if(F.row(k).sum()!=0)
                    ctf++;
            }
        }
        avg /= ctf;
        std::cout<<"try pos: "<<cd.rows()<<std::endl;
        for(int j=0;j<cd.rows();j++){
            Eigen::RowVector2d y;
            std::pair<bool,double> z;
            z = flip_avoid_line_search(V,uv,ring,v0,v1,cd.row(j),y,avg);
            std::cout<<"position "<<j<<": "<<std::get<0>(z)<<","<<std::get<1>(z)<<std::endl;
            int count = 0;
            if(std::get<0>(z) == true && std::get<1>(z)<maxenergy){
                found = true;
                pos << y(0),y(1);
                maxenergy = std::get<1>(z);
            }
        }
        if(found){
            auto F_t = F;
            int d = 0;
            for(int i=0;i<F_t.rows();i++){
                if(F.row(i).sum() != 0)
                    F_t.row(d++)<<F.row(i);
            }
            F_t.conservativeResize(d,3);
            std::cout<<"current face size "<<d<<"/"<<F.rows()<<std::endl;
            uv.row(v1) << pos;
            local_smoothing(V,F_t,B,uv,20,1e10);
        }else{
            F = F_store;
            uv = uv_store;
            // show the ring
            Eigen::MatrixXi F_show(ring.rows(),3);
            for(int i=0;i<ring.rows();i++){
                F_show.row(i)<<ring.row(i);
            }
            // std::cout<<AAk<<std::endl;
            uv.row(v1) = uv.row(v0);
            for(int r=0;r<ring.rows();r++){
                for(int k=0;k<3;k++){
                    std::cout<<ring(r,k)<<" ";
                }
                std::cout<<std::endl;
            }
            std::map<int,int> ring_to_dump;
            int c = 0;
            Eigen::MatrixXi local_F = ring;
            for(int t=0;t<ring.rows();t++){
                for(int k=0;k<3;k++)
                    if(ring_to_dump.find(ring(t,k))==ring_to_dump.end())
                        ring_to_dump[ring(t,k)] = c++;
            }
            
            for(int i=0;i<ring.rows();i++){
                for(int k=0;k<3;k++){
                    local_F(i,k)=ring_to_dump[ring(i,k)];
                    std::cout<<ring_to_dump[ring(i,k)]<<" ";
                }
                std::cout<<std::endl;
            }
            Eigen::MatrixXd local_v(ring_to_dump.size(),2);
            Eigen::MatrixXd local_v3(ring_to_dump.size(),3);
            for(auto p: ring_to_dump){
                local_v3.row(p.second)<<V.row(p.first);
                local_v.row(p.second)<<uv.row(p.first);
            }
            Eigen::MatrixXd CN;
            Eigen::MatrixXi FN;
            igl::writeOBJ("small_region.obj",local_v3,local_F,CN,FN,local_v,local_F);
            //igl::writeOFF("small_region.off",local_v,local_F);
            std::streamsize ss = std::cout.precision();
            std::cout<<std::setprecision(17)<<local_v<<std::endl;
            std::cout << std::setprecision(6);
            // std::cout<<"the collapse before this"<<std::endl;
            Eigen::MatrixXi ring_prev = std::get<2>(L[ii+1]);
            std::cout<<ring_prev<<std::endl;
            uv = uv_store;
            auto F_t = F;
            int d = 0;
            for(int i=0;i<F_t.rows();i++){
                if(F.row(i).sum() != 0)
                    F_t.row(d++)<<F.row(i);
            }
            F_t.conservativeResize(d,3);
            auto uv_o = uv;
            local_smoothing(V,F_t,B,uv,100,1e10);
            ii--;
        }
        double time2 = timer.getElapsedTime();
        total_time += (time2 - time1);
        double expect_total_time = (total_time / (ii+1)) * L.size();
        std::cout<<"expect time: "<<(expect_total_time-total_time)/60.0<<" mins "<<std::endl;
    } 
    return true;
}

bool progressive_embedding(
  const Eigen::MatrixXd& V,
  Eigen::MatrixXi& F,
  Eigen::MatrixXd& uv,
  const Eigen::VectorXi& bi,
  const Eigen::MatrixXd& b,
  double eps
){

  // [ Reference shape area for Dirichlet Energy ]
  double area_total = .0;
  Eigen::VectorXd area;
  igl::doublearea(uv,F,area);
  double avg = 0;
  for(int k=0;k<area.rows();k++){
    if(std::isfinite(area(k))){
      area_total += area(k);
    }
  }
  avg = area_total / F.rows();

  // [ Mark boundary vertices ]
  Eigen::VectorXi B;
  B.setZero(V.rows());
  for(int i=0;i<bi.rows();i++)
    B(bi(i)) = 1;

  // [ collect invalid elements ]
  Eigen::VectorXd E(F.rows());
  Eigen::Matrix<double,2,3> G;
  grad_to_eqtri(avg,G);
  for(int i=0;i<F.rows();i++){
    Eigen::Matrix<double,3,2> T;
    T<<uv.row(F(i,0)),uv.row(F(i,1)),uv.row(F(i,2));
    E(i) = autogen::sd_energy(T,G);
  }
  Eigen::VectorXi I;
  flipped_elements(uv,F,I);
  for(int i=0;i<F.rows();i++){
    if(!std::isfinite(E(i))||E(i)>eps)
      I(i)=1;
  }
  
  std::vector<Action> L; // list of collapse operation stored
  collapse_invalid_elements(V,F,uv,I,B,eps,avg,L);

  // [ invert vertex in reverse order of L ]
  std::reverse(L.begin(),L.end());
  insert_vertex_back(L,B,V,F,uv,area_total);
  
  // [ check result ]
  Eigen::VectorXi T;
  count_flipped_element(uv,F,T);
  std::cout<<"flipped: "<<T.sum()<<std::endl;
  igl::opengl::glfw::Viewer vr;
  vr.data().set_mesh(uv,F);
  vr.launch();
  return true;
}