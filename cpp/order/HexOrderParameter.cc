#include "HexOrderParameter.h"
#include "ScopedGILRelease.h"

#include <stdexcept>
#include <complex>
#include <tbb/tbb.h>

using namespace std;
using namespace boost::python;
using namespace tbb;

/*! \file HexOrderParameter.h
    \brief Compute the hexatic order parameter for each particle
*/

namespace freud { namespace order {

HexOrderParameter::HexOrderParameter(float rmax, float k=6)
    : m_box(trajectory::Box()), m_rmax(rmax), m_k(k), m_Np(0)
    {
    m_nn = new locality::NearestNeighbors();
    }

HexOrderParameter::~HexOrderParameter()
    {
    delete m_nn;
    }

void HexOrderParameter::updateBox(trajectory::Box& box)
    {
    // check to make sure the provided box is valid
    if (m_rmax > box.getLx()/2 || m_rmax > box.getLy()/2)
        throw invalid_argument("rmax must be smaller than half the smallest box size");
    if (m_rmax > box.getLz()/2 && !box.is2D())
        throw invalid_argument("rmax must be smaller than half the smallest box size");
    // see if it is different than the current box
    bool isUpdated = ( (m_box.getL() != box.getL()) ||
                       (m_box.getTiltFactorXY() != box.getTiltFactorXY()) ||
                       (m_box.getTiltFactorXZ() != box.getTiltFactorXZ()) ||
                       (m_box.getTiltFactorYZ() != box.getTiltFactorYZ()) );
    if (isUpdated)
        {
        m_box = box;
        locality::NearestNeighbors* tmp = new locality::NearestNeighbors(m_box, m_rmax, m_k);
        delete m_nn;
        m_nn = tmp;
        }
    }

class ComputeHexOrderParameter
    {
    private:
        const trajectory::Box& m_box;
        const float m_rmax;
        const float m_k;
        const locality::NearestNeighbors *m_nn;
        const vec3<float> *m_points;
        std::complex<float> *m_psi_array;
    public:
        ComputeHexOrderParameter(std::complex<float> *psi_array,
                                 const trajectory::Box& box,
                                 const float rmax,
                                 const float k,
                                 const locality::NearestNeighbors *nn,
                                 const vec3<float> *points)
            : m_box(box), m_rmax(rmax), m_k(k), m_nn(nn), m_points(points), m_psi_array(psi_array)
            {
            }

        void operator()( const blocked_range<size_t>& r ) const
            {
            float rmaxsq = m_rmax * m_rmax;

            for(size_t i=r.begin(); i!=r.end(); ++i)
                {
                m_psi_array[i] = 0;
                vec3<float> ref = m_points[i];
                boost::shared_array<unsigned int> neighbors = m_nn->getNeighbors(i);

                //loop over neighbors
                for (unsigned int neigh_idx = 0; neigh_idx < m_k; neigh_idx++)
                    {
                    unsigned int j = neighbors[neigh_idx];

                    //compute r between the two particles
                    vec3<float> delta = m_box.wrap(m_points[j] - ref);

                    float rsq = dot(delta, delta);
                    if (rsq > 1e-6)
                        {
                        //compute psi for neighboring particle(only constructed for 2d)
                        float psi_ij = atan2f(delta.y, delta.x);
                        m_psi_array[i] += exp(complex<float>(0,m_k*psi_ij));
                        }
                    }
                m_psi_array[i] /= complex<float>(m_k);
                }
            }
    };

void HexOrderParameter::compute(const vec3<float> *points, unsigned int Np)
    {
    // compute the cell list
    m_nn->compute(points,Np);

    // reallocate the output array if it is not the right size
    if (Np != m_Np)
        {
        m_psi_array = boost::shared_array<complex<float> >(new complex<float> [Np]);
        }

    // compute the order parameter
    parallel_for(blocked_range<size_t>(0,Np), ComputeHexOrderParameter(m_psi_array.get(), m_box, m_rmax, m_k, m_nn, points));

    // save the last computed number of particles
    m_Np = Np;
    }

void HexOrderParameter::computePy(trajectory::Box& box,
                                  boost::python::numeric::array points)
    {
    //validate input type and rank
    updateBox(box);
    num_util::check_type(points, PyArray_FLOAT);
    num_util::check_rank(points, 2);

    // validate that the 2nd dimension is only 3
    num_util::check_dim(points, 1, 3);
    unsigned int Np = num_util::shape(points)[0];

    // get the raw data pointers and compute order parameter
    vec3<float>* points_raw = (vec3<float>*) num_util::data(points);

        // compute the order parameter with the GIL released
        {
        util::ScopedGILRelease gil;
        compute(points_raw, Np);
        }
    }

void export_HexOrderParameter()
    {
    class_<HexOrderParameter>("HexOrderParameter", init<float>())
        .def(init<float, float>())
        .def("getBox", &HexOrderParameter::getBox, return_internal_reference<>())
        .def("compute", &HexOrderParameter::computePy)
        .def("getPsi", &HexOrderParameter::getPsiPy)
        ;
    }

}; }; // end namespace freud::order


