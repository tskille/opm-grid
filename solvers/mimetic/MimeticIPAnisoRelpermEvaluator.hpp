//===========================================================================
//
// File: MimeticIPAnisoRelpermEvaluator.hpp
//
// Created: Mon Oct 19 10:22:22 2009
//
// Author(s): Atgeirr F Rasmussen <atgeirr@sintef.no>
//            B�rd Skaflestad     <bard.skaflestad@sintef.no>
//
// $Date$
//
// $Revision$
//
//===========================================================================

/*
  Copyright 2009 SINTEF ICT, Applied Mathematics.
  Copyright 2009 Statoil ASA.

  This file is part of The Open Reservoir Simulator Project (OpenRS).

  OpenRS is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OpenRS is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OpenRS.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPENRS_MIMETICIPANISORELPERMEVALUATOR_HEADER
#define OPENRS_MIMETICIPANISORELPERMEVALUATOR_HEADER


#include <algorithm>
#include <vector>

#include <boost/bind.hpp>

#include <dune/common/ErrorMacros.hpp>
#include <dune/common/SparseTable.hpp>

#include <dune/solvers/common/fortran.hpp>
#include <dune/solvers/common/blas_lapack.hpp>
#include <dune/solvers/common/Matrix.hpp>

namespace Dune {
    /// @class MimeticIPAnisoRelpermEvaluator<CellIter,dim,computeInverseIP>
    ///
    /// @brief
    ///    Defines a class template for computing a matrix
    ///    representation of the permeability-dependent inner product
    ///    @f$b(v,w) = (v, K^{-1}\,w)@f$ of the velocity vectors
    ///    @f$v@f$ and @f$w@f$.  The matrix entries are defined
    ///    through the mimetic finite difference method of Brezzi
    ///    et. al.
    ///
    /// @tparam CellIter
    ///    Iterator type through which cell data such as the volume,
    ///    centroid, and connecting faces may be accessed.  @code
    ///    CellIter @endcode is expected to expose the method @code
    ///    operator->() @endcode.
    ///
    /// @tparam dim
    ///    Physical dimension of geometric quantities.  Usually, @code
    ///    dim==3 @endcode in simulations on corner-point grid models.
    ///
    /// @tparam computeInverseIP
    ///    Whether or not to compute the @em inverse of the mimetic
    ///    inner product matrix.  Specifically, if @f$B@f$ is the
    ///    matrix representation of the mimetic inner product, then
    ///    setting @code computeInverseIP = true; @endcode means that
    ///    the @code evaluate() @endcode method computes @f$B^{-1}@f$
    ///    rather than @f$B@f$ itself.  This parameter is a concession
    ///    to hybrid discretization methods based on Schur complement
    ///    reduction which only need access to @f$B^{-1}@f$.  In the
    ///    mimetic case there is an explicit formula for said inverse.
    template<class CellIter, int dim, bool computeInverseIP> class MimeticIPAnisoRelpermEvaluator;

    /// @brief
    ///    Specialization of general class template for the case of
    ///    computing the inverse inner product.
    template<class CellIter, int dim>
    class MimeticIPAnisoRelpermEvaluator<CellIter,dim,true> {
    public:
        /// @brief
        ///    The element type of the matrix representation of the
        ///    mimetic inner product.  Assumed to be a floating point
        ///    type, and usually, @code Scalar @endcode is an alias
        ///    for @code double @endcode.
        typedef typename CellIter::Scalar Scalar;


        /// @brief Default constructor.
        MimeticIPAnisoRelpermEvaluator()
            : max_nf_(-1)
        {}


        /// @brief Constructor.
        ///
        /// @param [in] max_nf
        ///    Maximum number of faces/connections of any single cell
        ///    in the model.  Used to set the size of certain internal
        ///    working arrays.  A cell with @f$n_f@f$ faces results in
        ///    an inner product matrix of size @f$n_f \times n_f@f$.
        MimeticIPAnisoRelpermEvaluator(const int max_nf)
            : max_nf_       (max_nf         ),
              fa_           (max_nf * max_nf),
              t1_           (max_nf * dim   ),
              t2_           (max_nf * dim   ),
              second_term_  (               ),
	      n_            (               )
        {}


        /// @brief Initialization routine.
        ///
        /// @param [in] max_nf
        ///    Maximum number of faces/connections of any single cell
        ///    in the model.  Used to set the size of certain internal
        ///    working arrays.  A cell with @f$n_f@f$ faces results in
        ///    an inner product matrix of size @f$n_f \times n_f@f$.
        void init(const int max_nf)
        {
            max_nf_ = max_nf;
            std::vector<double>(max_nf * max_nf).swap(fa_);
            std::vector<double>(max_nf * dim   ).swap(t1_);
            std::vector<double>(max_nf * dim   ).swap(t2_);
        }


        /// @brief
        ///    Reserve internal space for storing values of (static)
        ///    IP contributions for given set of cells.
        ///
        /// @tparam Vector
        ///    Vector type, often @code std::vector<int> @endcode,
        ///    representing a set of sizes.
        ///
        /// @param [in] sz
        ///    Set of sizes.  Assumed to contain @f$n@f$ positive
        ///    values, each representing the number of faces of a
        ///    specific cell.  In other words @code sz[i] @endcode is
        ///    the number of faces of cell @code i @endcode.
        template<class Vector>
        void reserveMatrices(const Vector& sz)
        {
            typedef typename Vector::value_type vt;

            Vector sz2(sz.size());

            std::transform(sz.begin(), sz.end(), sz2.begin(),
                           boost::bind(std::multiplies<vt>(), _1, _1));

            second_term_.allocate(sz2.begin(), sz2.end());

            std::transform(sz.begin(), sz.end(), sz2.begin(),
                           boost::bind(std::multiplies<vt>(), _1, dim));

            n_.allocate(sz2.begin(), sz2.end());
        }


        /// @brief
        ///    Main evaluation routine.  Computes the inverse of the
        ///    matrix representation of the mimetic inner product in a
        ///    single cell with kown permeability @f$K@f$.  Adds a
        ///    regularization term in order to guarantee a positive
        ///    definite matrix.
        ///
        /// @tparam RI
        ///    Type representing reservoir properties.  Assumed to
        ///    expose a method @code permeability(i) @endcode which
        ///    retrieves the static permeability tensor of cell @code
        ///    i @endcode.  The permeability tensor, @$K@$, is in
        ///    turn, assumed to expose a method @code operator()(int
        ///    i, int j) @endcode such that the call @code K(i,j)
        ///    @endcode retrieves the @f$ij@f$'th component of the
        ///    cell permeability @f$K@f$.
        ///
        /// @param [in] c
        ///    Cell for which to evaluate the inverse of the mimetic
        ///    inner product.
        ///
        /// @param [in] r
        ///    Specific reservoir properties.  Only the permeability
        ///    is used in method @code buildMatrix() @endcode.
        ///
        /// @param [in] nf
        ///    Number of faces (i.e., number of neighbours) of cell
        ///    @code *c @endcode.
        template<typename RI>
        void buildMatrix(const CellIter& c, const RI& r, const int nf)
        {
	    // Binv = (N*lambda*K*N'   +   t*diag(A)*(I - Q*Q')*diag(A))/vol
	    //         ^                     ^^^^^^^^^^^^^^^^^^^^^^^^^^
	    //         precompute: n_        precompute: second_term_
	    // t = 6/dim * trace(lambda*K)

            typedef typename CellIter::FaceIterator FI;
            typedef typename CellIter::Vector       CV;
            typedef typename FI      ::Vector       FV;

            const int ci = c->index();

            ASSERT (FV::size        == dim);
            ASSERT (int(t1_.size()) >= nf * dim);
            ASSERT (int(t2_.size()) >= nf * dim);
            ASSERT (int(fa_.size()) >= nf * nf);

            // SharedFortranMatrix T1  (nf, dim, &t1_      [0]);
            SharedFortranMatrix T2  (nf, dim, &t2_      [0]);
            SharedFortranMatrix fa  (nf, nf , &fa_      [0]);
            SharedFortranMatrix second_term(nf, nf , &second_term_[ci][0]);
            SharedFortranMatrix n(nf, nf , &n_[ci][0]);

            // Clear matrices of any residual data.
            zero(second_term);  zero(n);  zero(T2);  zero(fa);

            // Setup: second_term <- I, n <- N, T2 <- C
            const CV cc = c->centroid();
            int i = 0;
            for (FI f = c->facebegin(); f != c->faceend(); ++f, ++i) {
                second_term(i,i) = Scalar(1.0);
                fa(i,i)   = f->area();

                FV fc = f->centroid();  fc -= cc;  fc *= fa(i,i);
                FV fn = f->normal  ();             fn *= fa(i,i);

                for (int j = 0; j < dim; ++j) {
                    n(i,j) = fn[j];
                    T2(i,j) = fc[j];
                }
            }
            ASSERT (i == nf);

            // T2 <- orth(T2)
            if (orthogonalizeColumns(T2) != 0) {
                ASSERT (false);
            }

            // second_term <- second_term - T2*T2' == I - Q*Q'
            symmetricUpdate(Scalar(-1.0), T2, Scalar(1.0), second_term);

            // second_term <- diag(A) * second_term * diag(A)
            symmetricUpdate(fa, second_term);
        }


        template<class RI, class Sat>
        void computeDynamicParams(const CellIter&         c,
                                  const RI&               r,
                                  const std::vector<Sat>& s)
        {
            const int ci = c->index();

            boost::array<Scalar,RI::NumberOfPhases> mob ;
            boost::array<Scalar,RI::NumberOfPhases> rho ;
            r.phaseMobility(ci, s[ci],              mob);
            r.phaseDensity (ci,                     rho);

	    // lambdaK_ -> total_mobility*K
	    SharedFortranMatrix lambda(dim, dim, lambda_.data());
	    r.anisoTotalMobility(ci, s[ci], lambda);
	    typename RI::PermTensor K = r.permeability(ci);
	    SharedFortranMatrix lambdaK(dim, dim, lambdaK_.data());
	    prod(lambda, K, lambdaK);

	    // omega not yet changed.
            omega_  = std::inner_product(rho.begin(), rho.end(), mob.begin(),
                                         Scalar(0.0)) / r.totalMobility(ci, s[ci]);
        }


        /// @brief
        ///    Retrieve the dynamic (mobility updated) inverse mimetic
        ///    inner product matrix for specific cell.
        ///
        /// @tparam RI
        ///    Type representing reservoir properties.  Assumed to
        ///    expose a method @code phaseMobility(i,s,mob) @endcode
        ///    which retrieves the phase mobilities of all phases
        ///    evaluated at the saturations @code s @endcode.
        ///
        /// @tparam SP
        ///    Type representing the @code FullMatrix<T,SP,OP>
        ///    @endcode storage policy of the matrix into which the
        ///    inverse inner product matrix entries will be stored.
        ///
        /// @param [in] c
        ///    Cell for which to evaluate the dynamic inverse mimetic
        ///    inner product.
        ///
        /// @param [in] r
        ///    Specific reservoir properties.  Only the phase
        ///    mobilities is used in method @code getInverseMatrix()
        ///    @endcode.
        ///
        /// @param [in] s
        ///    Fluid saturations.
        ///
        /// @param [out] Binv
        ///    Inverse of matrix representation of the mimetic inner
        ///    product for cell @code *c @endcode.  A square, full
        ///    matrix with the number of rows equal to the number of
        ///    faces in cell @code *c @endcode.
        template<template<typename> class SP>
        void getInverseMatrix(const CellIter&                        c,
                              FullMatrix<Scalar,SP,FortranOrdering>& Binv) const
        {
	    // Binv = (N*lambda*K*N'   +   t*diag(A)*(I - Q*Q')*diag(A))/vol
	    //         ^                     ^^^^^^^^^^^^^^^^^^^^^^^^^^
	    //         precomputed: n_       precomputed: second_term_
	    // t = 6/dim * trace(lambda*K)
	    int ci = c->index();
	    int nf = Binv.numRows();
	    ImmutableFortranMatrix n(nf, dim, &n_[ci][0]);
            ImmutableFortranMatrix t2(nf, nf, &second_term_[ci][0]);
	    Binv = t2;
	    ImmutableFortranMatrix lambdaK(dim, dim, lambdaK_.data());
            SharedFortranMatrix T2(nf, dim, &t2_[0]);

            // T2 <- N*lambda*K
            matMulAdd_NN(Scalar(1.0), n, lambdaK, Scalar(0.0), T2);

            // Binv <- (T2*N' + t*Binv) / vol(c)
            //      == (N*lambda*K*N' + t*(diag(A) * (I - Q*Q') * diag(A))) / vol(c)
            //
            // where t = 6/d * TRACE(lambda*K) (== 2*TRACE(lambda*K) for 3D).
            //
            Scalar t = Scalar(6.0) * trace(lambdaK) / dim;
            matMulAdd_NT(Scalar(1.0) / c->volume(), T2, n,
                         t           / c->volume(), Binv  );
        }

        /// @brief
        ///    Computes the mimetic discretization of the gravity term
        ///    in Darcy's law.
        ///
        /// @tparam Point
        ///    Type representing a geometric point or a vector between
        ///    geometric points (e.g., a geometric direction and
        ///    distance) in the discretized model.  Usually @code
        ///    Point @endcode is an alias for a vector whose size is
        ///    known at compile time, such as @code FieldVector<T,3>
        ///    @endcode.
        ///
        /// @tparam Vector
        ///    Type representing a possibly run-time sized
        ///    one-dimensional mathematical vector.
        ///
        /// @param [in] c
        ///    Cell for which to evaluate the inverse of the mimetic
        ///    inner product.
        ///
        /// @param [in] grav
        ///    Gravity vector.
        ///
        /// @param [in] omega
        ///    The value of @f$\omega = \sum_i \rho_i f_i@f$ in cell
        ///    @code *c @endcode where @f$\rho_i@f$ and @f$f_i =
        ///    \lambda_i / \sum_j \lambda_j@f$ are, respectively, the
        ///    @em density and the saturation dependent <em>fractional
        ///    flow</em> of fluid @f$i@f$.
        ///
        /// @param [out] gterm
        ///    Mimetic discretization of the Darcy law gravity term.
        ///    One scalar value for each face of cell @code *c
        ///    @endcode.
        template<class Point, class Vector>
        void gravityTerm(const CellIter& c,
                         const Point&    grav,
                         const Scalar    omega,
                         Vector&         gterm) const
        {
            typedef typename CellIter::FaceIterator FI;

            ASSERT (gterm.size() <= max_nf_);

            const Point cc = c->centroid();
            int i = 0;
            for (FI f = c->facebegin(); f != c->faceend(); ++f, ++i) {
                Point fc = f->centroid();
                fc -= cc;
                gterm[i] = omega * (fc * grav);
            }
        }

        template<class Point, class Vector>
        void gravityTerm(const CellIter& c,
                         const Point&    grav,
                         Vector&         gterm) const
        {
            gravityTerm(c, grav, omega_, gterm);
        }

        template<class RI, class Sat, class Point, class Vector>
        void gravityTerm(const CellIter&         c,
                         const RI&               r,
                         const std::vector<Sat>& s,
                         const Point&            grav,
                         Vector&                 gterm) const
        {
            const int ci = c->index();

            boost::array<Scalar,RI::NumberOfPhases> mob ;
            boost::array<Scalar,RI::NumberOfPhases> rho ;
            r.phaseMobility(ci, s[ci],              mob);
            r.phaseDensity (ci,                     rho);

            Scalar totmob = std::accumulate   (mob.begin(), mob.end(), Scalar(0.0));
            Scalar omega  = std::inner_product(rho.begin(), rho.end(), mob.begin(),
                                               Scalar(0.0)) / totmob;

            gravityTerm(c, grav, omega, gterm);
        }

    private:
        int                 max_nf_      ;
        Scalar              omega_       ;
        mutable std::vector<Scalar> fa_, t1_, t2_;
        SparseTable<Scalar> second_term_ ;
        SparseTable<Scalar> n_           ;
	boost::array<double, dim*dim> lambda_;
	boost::array<double, dim*dim> lambdaK_;
    };
} // namespace Dune


#endif // OPENRS_MIMETICIPANISORELPERMEVALUATOR_HEADER
