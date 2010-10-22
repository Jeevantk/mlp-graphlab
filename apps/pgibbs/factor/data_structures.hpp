#ifndef DATA_STRUCTURES_HPP
#define DATA_STRUCTURES_HPP

/**
 *
 * Parallel blocked gibbs using graphlab
 *
 *  \author Joseph Gonzalez
 */

// INCLUDES ===================================================================>


#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cassert>



// Including Standard Libraries

#include <graphlab.hpp>

#include <graphlab/macros_def.hpp>



typedef graphlab::vertex_id_t     vertex_id_t;
typedef graphlab::edge_id_t       edge_id_t;

const size_t MAX_DIM = 10;
typedef graphlab::variable              variable_t;
typedef graphlab::table_factor<MAX_DIM> factor_t;
typedef factor_t::domain_type           domain_t;
typedef factor_t::assignment_type       assignment_t;


// Represents a null VID in the tree
const vertex_id_t NULL_VID = -1;



// STRUCTS (Edge and Vertex data) =============================================>

struct vertex_data {

  enum vertex_state {
    AVAILABLE,         // Vertex is completely available [Default]

    CANDIDATE,         // The vertex is currently a candidate
    
    BOUNDARY,          // The vertex is on the boundary of a tree

    TREE_NODE,         // The vertex is in a tree
    
    CALIBRATED         // The vertex has been calibrated and has 
                       // computed the message to the parent 
  };


  // Problem specific variables
  variable_t     variable;
  assignment_t   asg;
  std::vector<size_t> factor_ids;
  factor_t       belief;
  
  factor_t       tmp_bp_belief;
  
  size_t         updates;


  
  // Tree construction variables
  vertex_id_t    parent;
  vertex_state   state;
  size_t         marked_up;
  size_t         height;

  graphlab::atomic<size_t> child_candidates;
  
  vertex_data() :
    updates(0),
    parent(NULL_VID),
    state(AVAILABLE),
    height(0),
    child_candidates(0){ }

  vertex_data(const variable_t& variable,
              const std::vector<size_t>& factor_ids) :
    variable(variable),
    asg(variable, std::rand() % variable.arity),
    factor_ids(factor_ids),
    belief(domain_t(variable)),
    tmp_bp_belief(domain_t(variable)),
    updates(0),
    parent(NULL_VID),
    state(AVAILABLE),
    height(0),
    child_candidates(0) {
    // Set the belief to uniform 0
    belief.uniform(-std::numeric_limits<double>::max());
    assert(!factor_ids.empty());
  }


  

  void save(graphlab::oarchive &arc) const {
    arc << variable;
    arc << asg;
    arc << factor_ids;
    arc << belief;
    arc << tmp_bp_belief;
    arc << updates;

    arc << parent;
    arc.o->write(reinterpret_cast<const char*>(&state), sizeof(vertex_state));
    arc << height;
    size_t value = child_candidates.value;
    arc << value;
  }
  
  void load(graphlab::iarchive &arc) {
    arc >> variable;
    arc >> asg;
    arc >> factor_ids;
    arc >> belief;
    arc >> tmp_bp_belief;
    arc >> updates;

    arc >> parent;
    arc.i->read(reinterpret_cast<char*>(&state), sizeof(vertex_state));
    arc >> height;
    size_t value;
    arc >> value;
    child_candidates.value = value;
  }
}; // End of vertex data













/**
 * The data associated with each directed edge in the pairwise markov
 * random field
 */
struct edge_data { 
  double     weight;
  factor_t   message;
  factor_t   edge_factor;
  bool     exploring;
  
  edge_data() : weight(0), exploring(false) { }
  edge_data(const variable_t& source, const variable_t& target) :
    weight(0), message( domain_t(target) ),
    edge_factor( domain_t(source, target) ),
    exploring(false) { }

  void save(graphlab::oarchive &arc) const {
    arc << weight;
    arc << message;
    arc << edge_factor;
    arc << exploring;
  }
  
  void load(graphlab::iarchive &arc) {
    arc >> weight;
    arc >> message;
    arc >> edge_factor;
    arc >> exploring;
  }
}; 










// define the graph type:
typedef graphlab::graph< vertex_data, edge_data> graph_type;
typedef graphlab::types<graph_type> gl;

















// A class which represents a factorized distribution as a collection
// of factors.
class factorized_model {
public:
  void add_factor(const factor_t& factor) {
    _factors.push_back(factor);
    size_t factor_id = _factors.size() - 1;
    for(size_t i = 0; i < factor.num_vars(); ++i) {
      variable_t var = factor.args().var(i); 
      _variables.insert(var);
      // add factor to reverse map
      _var_to_factor[var].push_back(factor_id);
    }
  }

  
  const std::vector<factor_t>& factors() const { return _factors; }
  const std::set<variable_t>& variables() const { return _variables; }

  const std::vector<size_t>& factor_ids(const variable_t& var) const {
    typedef std::map<variable_t, std::vector<size_t> >::const_iterator iterator;
    iterator iter = _var_to_factor.find(var);
    assert(iter != _var_to_factor.end());
    return iter->second;
  }

  const std::string& var_name(size_t id) const {
    assert(id < _var_name.size());
    return _var_name[id];
  }

  
  void load_alchemy(const std::string& filename) {
    // Open an input file stream
    std::ifstream fin(filename.c_str());
    assert(fin.good());
    std::string line;
    size_t line_number = 0;
    // Read the first line which should be "variable:"
    assert(getline(fin,line,line_number++));
    line = trim(line);
    assert(line == "variables:");
    // Read all the variables and create a map from the variable name
    // (string) to the variable* prl variable pointer.
    typedef std::map<std::string, variable_t> var_map_type;
    typedef var_map_type::iterator var_map_iter_type;
    var_map_type var_map;
    size_t unique_var_id = 0;
    while(fin.good() &&
          getline(fin, line, line_number++) &&
          trim(line) != "factors:") {
      // Separate into name and size
      line = trim(line);
      assert(line.length() > 0);
      size_t namelen = line.find_last_of('\t');
      size_t varsize = 2;
      // if their is a '\t' character then the variable size follows it
      if(namelen != std::string::npos) {
        std::stringstream istrm(trim(line.substr(namelen)));
        istrm >> varsize;
      }
      // Get the variable name
      std::string var_name = trim(line.substr(0, namelen));
      // Create a new finite variable in the universe
      variable_t variable(unique_var_id++, varsize);
      // Store the variable in the local variable map
      var_map[var_name] = variable;
      _var_name.push_back(var_name);
      assert(_var_name.size() == unique_var_id);
    }

    // Starting to read factors
    assert(trim(line) == "factors:");

    while(fin.good() && getline(fin, line, line_number++)) {
      /// if the line is empty skip it
      if(trim(line).length() == 0) continue;
      //      std::cout << "Line: " << line << std::endl;
      // the factor being read may contain the same variable multiple
      // times to account for that, we first read a temporary factors,
      // making every variable unique artificially, and then convert
      // it to the factor we actually need

      // Process the arguments
      size_t end_of_variables = line.find("//")-1;
      std::vector<variable_t> args;
      std::set<variable_t> args_set;

      // Read in all the variables in the factor and store them
      for(size_t i = 0; i < end_of_variables;
          i = line.find_first_of('/', i) + 1) {
        // Read the next variable as a string
        std::string variable_name =
          trim(line.substr(i, line.find_first_of('/',i) - i));
        //        std::cout << "Variable Name: " << variable_name << std::endl;
        // Look up the variable in the variable map
        var_map_iter_type iter = var_map.find(variable_name);
        assert(iter != var_map.end());
        variable_t var = iter->second;
        // This argument must be unique
        if(args_set.count(var) > 0) {
          std::cout << "Line Number: " << line_number << std::endl;
          assert(args_set.count(var) == 0);
        }

        args_set.insert(var);
        // Save the arguments read from the file
        args.push_back(var);
      } // end of first pass through variable

      // Construct the arguments (which will remap the domain)
      domain_t domain(args);
      //      std::cout << "domain: " << domain << std::endl;
      // Build the factor
      factor_t factor(domain);
      
      // Now for the tricky part we need an assignment in the original
      // order
      domain_t orig_domain;
      for(size_t i = 0; i < args.size(); ++i) {
        orig_domain += variable_t(i, args[i].arity);
      }


      // Advance to the correct location in the line
      std::istringstream tbl_values;
      size_t weightpos = line.find("///");
      if (weightpos == std::string::npos) {
        tbl_values.str(line.substr(line.find("//") + 2));
      } else {
        size_t startpos = line.find("//") + 2;
        tbl_values.str(line.substr(startpos, weightpos - startpos));
      }
      
      // Read in the weights
      for(assignment_t orig_asg = orig_domain.begin();
          orig_asg < orig_domain.end(); ++orig_asg) {
        assignment_t asg(domain);
        // Translate the original assignment into the sorted factor assignment
        for(size_t i = 0; i < domain.num_vars(); ++i) {
          size_t variable_id = args[i].id;
          asg.set_asg(variable_id, orig_asg.asg(i));
        }
        // Read then next value
        assert(tbl_values.good());
        double value = 0;
        tbl_values >> value;
        // Values are stored in log form      
        factor.logP(asg.linear_index()) = value;                
      }

      // Save the factor to the factor graph
      add_factor(factor);                
    } // End of outer while loop over factors should be end of file

    assert(fin.good() == false);
    fin.close();
  } // end of load alchemy

  //! Save the factor to a file
  void save(graphlab::oarchive &arc) const {
    arc << _variables
        << _factors
        << _var_to_factor
        << _var_name;
  }

  //! Load the factor from a file
  void load(graphlab::iarchive &arc) {
    arc >> _variables
        >> _factors
        >> _var_to_factor
        >> _var_name;
  }


  

  
  
private:
  std::set<variable_t> _variables;
  std::vector<factor_t> _factors;
  std::map< variable_t, std::vector<size_t> > _var_to_factor;
  std::vector<std::string> _var_name;

  
  /**
   * same as the stl string get line but this also increments a line
   * counter which is useful for debugging purposes
   */
  inline bool getline(std::ifstream& fin,
                      std::string& line,
                      size_t line_number) {
    return std::getline(fin, line).good();
  }
  
  /**
   * Removes trailing and leading white space from a string
   */
  inline std::string trim(const std::string& str) {
    std::string::size_type pos1 = str.find_first_not_of(" \t\r");
    std::string::size_type pos2 = str.find_last_not_of(" \t\r");
    return str.substr(pos1 == std::string::npos ? 0 : pos1,
                      pos2 == std::string::npos ? str.size()-1 : pos2-pos1+1);
  }

}; //end of factorized model















void construct_clique_graph(const factorized_model& model,
                            graph_type& graph) {
  // Add all the variables
  foreach(variable_t variable, model.variables()) {
    vertex_data vdata(variable, model.factor_ids(variable));
    // start with an initial random assignment
    vdata.asg.uniform_sample();
    graphlab::vertex_id_t vid = graph.add_vertex(vdata);
    // We require variable ids to match vertex id (this simplifies a
    // lot of stuff).
    assert(vid == variable.id);
  }  
  assert(graph.num_vertices() == model.variables().size());
  const std::vector<factor_t>& factors = model.factors();
  // Add all the edges
  for(vertex_id_t vid = 0; vid < graph.num_vertices(); ++vid) {
    const vertex_data& vdata  = graph.vertex_data(vid);
    // Compute all the neighbors of this vertex by looping over all
    // the variables in all the factors that contain this vertex
    std::set<variable_t> neighbors;
    foreach(size_t fid, vdata.factor_ids) {
      const domain_t& args = factors[fid].args();
      for(size_t n = 0; n < args.num_vars(); ++n) {
        variable_t neighbor_var = args.var(n);
        if(vdata.variable != neighbor_var )
          neighbors.insert(neighbor_var);
      }
    }
    // For each of those variables add an edge from this varaible to
    // that variable
    foreach(variable_t neighbor_variable, neighbors) {
      vertex_id_t neighbor_vid = neighbor_variable.id;
      edge_data edata(vdata.variable, neighbor_variable);
      graph.add_edge(vid, neighbor_vid, edata);      
    }
  } // loop over factors

} // End of construct_clique_graph












/** Save the beliefs stored in the graph */
void save_beliefs(const graph_type& graph,
                  const std::string& filename) {
  std::ofstream fout(filename.c_str());
  fout.precision(16);
  factor_t marginal;
  for(size_t v = 0; v < graph.num_vertices(); ++v) {
    const vertex_data& vdata = graph.vertex_data(v);
    marginal = vdata.belief;
    marginal.normalize();
    fout << vdata.updates << '\t';
    size_t arity = marginal.args().var(0).arity;
    for(size_t asg = 0; asg < arity; ++asg) {
      fout << std::exp( marginal.logP(asg) );
      if(asg + 1 < arity ) fout << '\t';      
    }
    fout << '\n';
  } 
  fout.close();
} // End of save beliefs


void save_asg(const graph_type& graph,
              const std::string& filename) {
  std::ofstream fout(filename.c_str());
  graphlab::unary_factor marginal;
  for(size_t v = 0; v < graph.num_vertices(); ++v) 
    fout << graph.vertex_data(v).asg.asg(v) << '\n';
  fout.close();
} // End of save beliefs


void save_color(const graph_type& graph,
                const std::string& filename) {
  std::ofstream fout(filename.c_str());
  graphlab::unary_factor marginal;
  for(size_t v = 0; v < graph.num_vertices(); ++v) 
    fout << graph.color(v) << '\n';
  fout.close();
} // End of save beliefs




void save_tree_state(const graph_type& graph,
                     const std::string& filename) {
  std::ofstream fout(filename.c_str());
  for(size_t v = 0; v < graph.num_vertices(); ++v) {
    const vertex_data& vdata = graph.vertex_data(v);
    fout << v << '\t'
         << vdata.state << '\t'
         << vdata.parent << '\n';
  } 
  fout.close();
} // End of save beliefs



void min_max_samples(const graph_type& graph,
                 size_t& min_samples,
                 size_t& max_samples) {
  max_samples = 0;
  min_samples = -1;
  for(size_t i = 0; i < graph.num_vertices(); ++i) {
    const vertex_data& vdata = graph.vertex_data(i);
    max_samples = std::max(max_samples, vdata.updates);
    min_samples = std::min(min_samples, vdata.updates);
  }
}



#include <graphlab/macros_undef.hpp>
#endif



