// SPDX-FileCopyrightText: Copyright (c) 2022 The Lethe Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception OR LGPL-2.1-or-later

/**
 * @brief Tests the constant tracer diffusivity model. This model should always return a constant.
 */

// Lethe
#include <core/tracer_diffusivity_model.h>

// Tests (with common definitions)
#include <../tests/tests.h>

void
test()
{
  deallog << "Beggining" << std::endl;


  ConstantTracerDiffusivity model(5);

  deallog << "Testing tracer diffusivity" << std::endl;

  // field values can remain empty since the constant thermal expansion does
  // not depend on any fields
  std::map<field, double> field_values;

  deallog << " T = 1    , tracer diffusivity = " << model.value(field_values)
          << std::endl;
  deallog << " T = 2    , tracer diffusivity = " << model.value(field_values)
          << std::endl;

  deallog << "OK" << std::endl;
}

int
main()
{
  try
    {
      initlog();
      test();
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
}
