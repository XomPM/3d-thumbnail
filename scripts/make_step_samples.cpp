#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <IGESControl_Writer.hxx>
#include <IGESData_IGESModel.hxx>
#include <StepBasic_Person.hxx>
#include <StepBasic_Organization.hxx>
#include <StepData_StepModel.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void write_step(const std::filesystem::path& path, const TopoDS_Shape& shape, const char* schema) {
    STEPControl_Writer writer;
    if (!Interface_Static::SetCVal("write.step.schema", schema)) {
        throw std::runtime_error("Cannot select STEP schema.");
    }
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
        throw std::runtime_error("Cannot transfer sample shape.");
    }
    // AP203 otherwise embeds the local account and host address in PERSON entities.
    for (int index = 1; index <= writer.Model()->NbEntities(); ++index) {
        const auto person = Handle(StepBasic_Person)::DownCast(writer.Model()->Value(index));
        if (!person.IsNull()) {
            person->SetId(new TCollection_HAsciiString("3DThumbnail"));
            person->SetFirstName(new TCollection_HAsciiString("DMIND"));
            person->SetLastName(new TCollection_HAsciiString("Samples"));
        }
        const auto organization = Handle(StepBasic_Organization)::DownCast(writer.Model()->Value(index));
        if (!organization.IsNull()) {
            organization->SetId(new TCollection_HAsciiString("3DThumbnail"));
            organization->SetName(new TCollection_HAsciiString("3DThumbnail samples"));
        }
    }
    if (std::filesystem::exists(path)) { throw std::runtime_error("Refusing to overwrite an existing sample."); }
    std::ofstream output(path, std::ios::binary);
    if (!output || writer.WriteStream(output) != IFSelect_RetDone || !output) {
        throw std::runtime_error("Cannot write STEP sample.");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("Usage: make-step-samples <output-directory>"); }
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto base = BRepPrimAPI_MakeBox(gp_Pnt(-30, -22, 0), 60, 44, 5).Shape();
        const auto outer = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1)), 17, 25).Shape();
        const auto bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 5), gp_Dir(0, 0, 1)), 10, 25).Shape();
        const auto bearing = BRepAlgoAPI_Fuse(base, BRepAlgoAPI_Cut(outer, bore).Shape()).Shape();
        write_step(directory / "bearing_ap214.step", bearing, "AP214IS");
        write_step(directory / "bearing_ap203.stp", bearing, "AP203");
        write_step(directory / "bearing_ap242.step", bearing, "AP242DIS");
        for (const auto mode : {0, 1}) {
            IGESControl_Writer writer("MM", mode);
            if (!writer.AddShape(bearing)) { throw std::runtime_error("Cannot convert IGES fixture."); }
            writer.Model()->ChangeGlobalSection().SetAuthorName(new TCollection_HAsciiString("DMIND"));
            writer.Model()->ChangeGlobalSection().SetCompanyName(new TCollection_HAsciiString("3DThumbnail"));
            const auto path = directory / (mode ? "bearing_brep.igs" : "bearing.iges");
            if (std::filesystem::exists(path)) { throw std::runtime_error("Refusing to overwrite an existing sample."); }
            std::ofstream output(path, std::ios::binary);
            if (!writer.Write(output) || !output) { throw std::runtime_error("Cannot write IGES fixture."); }
        }

        BRep_Builder builder;
        TopoDS_Compound assembly;
        builder.MakeCompound(assembly);
        builder.Add(assembly, bearing);
        gp_Trsf offset;
        offset.SetTranslation(gp_Vec(100, 20, 0));
        builder.Add(assembly, bearing.Moved(TopLoc_Location(offset)));
        write_step(directory / "placed_solids.step", assembly, "AP214IS");
        std::cout << "Created original AP203, AP214, AP242 and placed-solid STEP samples.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
