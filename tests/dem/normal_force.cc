// SPDX-FileCopyrightText: Copyright (c) 2020-2025 The Lethe Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception OR LGPL-2.1-or-later

/**
 * @brief This test reports the normal overlap and corresponding normal force
 * during a complete particle-wall contact. Interested reader may be
 * interested in plotting normal force against normal overlap.
 */

// Deal.II includes
#include <deal.II/base/parameter_handler.h>

#include <deal.II/fe/mapping_q.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/tria.h>

#include <deal.II/particles/particle.h>
#include <deal.II/particles/particle_handler.h>
#include <deal.II/particles/particle_iterator.h>

// Lethe
#include <core/dem_properties.h>

#include <dem/data_containers.h>
#include <dem/dem_solver_parameters.h>
#include <dem/find_boundary_cells_information.h>
#include <dem/particle_wall_broad_search.h>
#include <dem/particle_wall_contact_force.h>
#include <dem/particle_wall_fine_search.h>
#include <dem/particle_wall_nonlinear_force.h>
#include <dem/velocity_verlet_integrator.h>

// Tests (with common definitions)
#include <../tests/tests.h>

using namespace dealii;

template <int dim, typename PropertiesIndex>
void
test()
{
  // Creating the mesh and refinement
  parallel::distributed::Triangulation<dim> tr(MPI_COMM_WORLD);
  int                                       hyper_cube_length = 1;
  GridGenerator::hyper_cube(tr,
                            -1 * hyper_cube_length,
                            hyper_cube_length,
                            true);
  const double grid_radius       = 0.5 * GridTools::diameter(tr);
  int          refinement_number = 2;
  tr.refine_global(refinement_number);
  MappingQ<dim>            mapping(1);
  DEMSolverParameters<dim> dem_parameters;

  // Defining general simulation parameters
  Tensor<1, dim> g{{0, 0, 0}};
  double         dt                                                  = 0.000001;
  double         particle_diameter                                   = 0.001;
  unsigned int   rotating_wall_maximum_number                        = 6;
  dem_parameters.lagrangian_physical_properties.particle_type_number = 1;
  dem_parameters.lagrangian_physical_properties.youngs_modulus_particle[0] =
    200000000000;
  dem_parameters.lagrangian_physical_properties.youngs_modulus_wall =
    200000000000;
  dem_parameters.lagrangian_physical_properties.poisson_ratio_particle[0] = 0.3;
  dem_parameters.lagrangian_physical_properties.poisson_ratio_wall        = 0.3;
  dem_parameters.lagrangian_physical_properties
    .restitution_coefficient_particle[0] = 0.5;
  dem_parameters.lagrangian_physical_properties.restitution_coefficient_wall =
    0.5;
  dem_parameters.lagrangian_physical_properties
    .friction_coefficient_particle[0]                                     = 0.3;
  dem_parameters.lagrangian_physical_properties.friction_coefficient_wall = 0.3;
  dem_parameters.lagrangian_physical_properties
    .rolling_friction_coefficient_particle[0] = 0.1;
  dem_parameters.lagrangian_physical_properties
    .rolling_viscous_damping_coefficient_particle[0]                  = 0.1;
  dem_parameters.lagrangian_physical_properties.rolling_friction_wall = 0.1;
  dem_parameters.lagrangian_physical_properties.rolling_viscous_damping_wall =
    0.1;
  dem_parameters.lagrangian_physical_properties.density_particle[0] = 7850;
  dem_parameters.model_parameters.rolling_resistance_method =
    Parameters::Lagrangian::RollingResistanceMethod::constant_resistance;

  // Initializing motion of boundaries
  Tensor<1, dim> translational_and_rotational_velocity;
  for (unsigned int d = 0; d < dim; ++d)
    {
      translational_and_rotational_velocity[d] = 0;
    }
  for (unsigned int counter = 0; counter < rotating_wall_maximum_number;
       ++counter)
    {
      dem_parameters.boundary_conditions.boundary_rotational_speed.insert(
        {counter, 0});
      dem_parameters.boundary_conditions.boundary_translational_velocity.insert(
        {counter, translational_and_rotational_velocity});
      dem_parameters.boundary_conditions.boundary_rotational_vector.insert(
        {counter, translational_and_rotational_velocity});
    }

  // Defining particle handler
  Particles::ParticleHandler<dim> particle_handler(
    tr, mapping, PropertiesIndex::n_properties);
  // Inserting one particle in contact with wall
  Point<dim>               position1 = {-0.999, 0, 0};
  int                      id        = 0;
  Particles::Particle<dim> particle1(position1, position1, id);
  typename Triangulation<dim>::active_cell_iterator particle_cell =
    GridTools::find_active_cell_around_point(tr, particle1.get_location());
  Particles::ParticleIterator<dim> pit1 =
    particle_handler.insert_particle(particle1, particle_cell);
  pit1->get_properties()[PropertiesIndex::type]    = 0;
  pit1->get_properties()[PropertiesIndex::dp]      = particle_diameter;
  pit1->get_properties()[PropertiesIndex::v_x]     = -1.0;
  pit1->get_properties()[PropertiesIndex::v_y]     = 0;
  pit1->get_properties()[PropertiesIndex::v_z]     = 0;
  pit1->get_properties()[PropertiesIndex::omega_x] = 0;
  pit1->get_properties()[PropertiesIndex::omega_y] = 0;
  pit1->get_properties()[PropertiesIndex::omega_z] = 0;
  pit1->get_properties()[PropertiesIndex::mass]    = 1;

  std::vector<Tensor<1, 3>> torque;
  std::vector<Tensor<1, 3>> force;
  std::vector<double>       MOI;
  torque.push_back(Tensor<1, dim>({0, 0, 0}));
  force.push_back(Tensor<1, dim>({0, 0, 0}));
  MOI.push_back(1);
  double step_force;

  // Finding boundary cells
  BoundaryCellsInformation<dim> boundary_cells_object;
  std::vector<unsigned int>     outlet_boundaries;

  boundary_cells_object.build(
    tr,
    outlet_boundaries,
    false,
    ConditionalOStream(std::cout,
                       Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0));

  // P-W broad search
  typename DEM::dem_data_structures<dim>::particle_wall_candidates
    particle_wall_contact_list;
  find_particle_wall_contact_pairs<dim>(
    boundary_cells_object.get_boundary_cells_information(),
    particle_handler,
    particle_wall_contact_list);

  // Particle-Wall fine search
  typename DEM::dem_data_structures<dim>::particle_wall_in_contact
    particle_wall_contact_information;
  ParticleWallNonLinearForce<dim, PropertiesIndex> particle_wall_force_object(
    dem_parameters);
  VelocityVerletIntegrator<dim, PropertiesIndex> integrator_object;
  double                                         distance;
  double                                         time = 0.0;

  while (time < 0.00115)
    {
      auto particle = particle_handler.begin();
      force[0][0]   = 0;
      force[0][1]   = 0;
      if (dim == 3)
        {
          force[0][2] = 0;
        }
      distance = hyper_cube_length + particle->get_location()[0] -
                 particle->get_properties()[PropertiesIndex::dp] / 2.0;

      if (distance > 0.0)
        {
          // If particle and wall are not in contact, only the integration class
          // is called
          integrator_object.integrate(
            particle_handler, g, dt, torque, force, MOI);
        }
      else
        {
          // If particle and wall are in contact
          particle_wall_fine_search<dim>(particle_wall_contact_list,
                                         particle_wall_contact_information);
          auto particle_wall_pairs_in_contact_iterator =
            &particle_wall_contact_information.begin()->second;
          auto particle_wall_contact_information_iterator =
            particle_wall_pairs_in_contact_iterator->begin();

          particle_wall_contact_information_iterator->second
            .tangential_displacement[0] = 0.0;
          particle_wall_contact_information_iterator->second
            .tangential_displacement[1] = 0.0;

          if (dim == 3)
            {
              particle_wall_contact_information_iterator->second
                .tangential_displacement[2] = 0.0;
            }
          particle_wall_contact_information_iterator->second
            .tangential_relative_velocity[0] = 0.0;
          particle_wall_contact_information_iterator->second
            .tangential_relative_velocity[1] = 0.0;
          if (dim == 3)
            {
              particle_wall_contact_information_iterator->second
                .tangential_relative_velocity[2] = 0.0;
            }

          particle_wall_force_object.calculate_particle_wall_contact_force(
            particle_wall_contact_information, dt, torque, force);

          // Storing force before integration
          step_force = force[0][0];


          integrator_object.integrate(
            particle_handler, g, dt, torque, force, MOI);


          deallog
            << " "
            << particle_wall_contact_information_iterator->second.normal_overlap
            << " " << step_force << std::endl;
        }

      time += dt;
    }
}

int
main(int argc, char **argv)
{
  try
    {
      Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

      initlog();
      test<3, DEM::DEMProperties::PropertiesIndex>();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  return 0;
}
