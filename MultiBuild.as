void main(MultiBuild::Workspace& workspace) {	
	auto project = workspace.create_project(".");
	auto properties = project.properties();

	project.name("FastNoise2");
	properties.binary_object_kind(MultiBuild::BinaryObjectKind::eStaticLib);
	properties.cpp_dialect(MultiBuild::LangDialectCpp::e17);
	project.license("./LICENSE");

	project.include_own_required_includes(true);
	project.add_required_project_include({
		"./include"
	});

	properties.files({
		"./include/**.h",
		"./include/**.inl",
		"./src/**.h",
		"./src/**.cpp"
	});
	
	{
		MultiBuild::ScopedFilter _(project, "project.compiler:VisualCpp");
		properties.disable_warnings("4756");
	}
}