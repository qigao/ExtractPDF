#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

enum pdf_security_marker : uint32_t {
    pdf_security_marker_javascript = UINT32_C(1) << 0,
    pdf_security_marker_launch = UINT32_C(1) << 1,
    pdf_security_marker_external = UINT32_C(1) << 2,
    pdf_security_marker_other = UINT32_C(1) << 3,
    pdf_security_marker_embedded = UINT32_C(1) << 4,
    pdf_security_marker_xfa = UINT32_C(1) << 5,
    pdf_security_marker_rich_media = UINT32_C(1) << 6,
    pdf_security_marker_safe_goto = UINT32_C(1) << 7,
};

static QPDFObjectHandle pdf_security_marked_dictionary(
    QPDF& pdf,
    char const *marker)
{
    return pdf.makeIndirectObject(QPDFObjectHandle::newDictionary({
        {"/QuantaPDFMarker", QPDFObjectHandle::newString(marker)}}));
}

static QPDFObjectHandle pdf_security_marked_action(
    QPDF& pdf,
    char const *name,
    char const *marker)
{
    QPDFObjectHandle action = pdf_security_marked_dictionary(pdf, marker);
    action.replaceKey("/S", QPDFObjectHandle::newName(name));
    return action;
}

static QPDFObjectHandle pdf_security_action(QPDF& pdf, const char *name)
{
    return pdf.makeIndirectObject(QPDFObjectHandle::newDictionary({
        {"/S", QPDFObjectHandle::newName(name)}}));
}

static QPDFObjectHandle pdf_security_signature(QPDF& pdf)
{
    return pdf.makeIndirectObject(QPDFObjectHandle::newDictionary({
        {"/Type", QPDFObjectHandle::newName("/Sig")},
        {"/Contents", QPDFObjectHandle::newString("signed")}}));
}

static void pdf_security_make_combined_fixture(
    QPDF& pdf,
    QPDFObjectHandle root,
    QPDFObjectHandle page)
{
    QPDFObjectHandle javascript = pdf_security_marked_action(
        pdf, "/JavaScript", "javascript-action");
    QPDFObjectHandle launch = pdf_security_marked_action(
        pdf, "/Launch", "launch-action");
    QPDFObjectHandle external = pdf_security_marked_action(
        pdf, "/URI", "external-action");
    QPDFObjectHandle other = pdf_security_marked_action(
        pdf, "/Named", "other-action");
    QPDFObjectHandle safe = pdf_security_marked_action(
        pdf, "/GoTo", "safe-goto");
    QPDFObjectHandle nested = QPDFObjectHandle::newArray();
    nested.appendItem(pdf_security_marked_action(
        pdf, "/JavaScript", "javascript-next"));
    nested.appendItem(pdf_security_marked_action(
        pdf, "/Launch", "launch-next"));
    nested.appendItem(pdf_security_marked_action(
        pdf, "/GoTo", "safe-goto-next"));
    safe.replaceKey("/D", QPDFObjectHandle::newName("/SafeDestination"));
    safe.replaceKey("/Next", nested);

    root.replaceKey("/OpenAction", javascript);
    root.replaceKey(
        "/QuantaPDFLaunchOwner",
        QPDFObjectHandle::newDictionary({{"/A", launch}}));
    root.replaceKey(
        "/QuantaPDFExternalOwner",
        QPDFObjectHandle::newDictionary({
            {"/AA", QPDFObjectHandle::newDictionary({{"/E", external}})}}));
    root.replaceKey(
        "/QuantaPDFOtherOwner",
        QPDFObjectHandle::newDictionary({{"/A", other}}));
    root.replaceKey(
        "/QuantaPDFSafeOwner",
        QPDFObjectHandle::newDictionary({{"/A", safe}}));

    QPDFObjectHandle javascript_names = pdf_security_marked_dictionary(
        pdf, "javascript-names");
    QPDFObjectHandle embedded_stream = pdf.newStream("embedded-name-payload");
    embedded_stream.getDict().replaceKey(
        "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    embedded_stream.getDict().replaceKey(
        "/QuantaPDFMarker",
        QPDFObjectHandle::newString("embedded-name-stream"));
    QPDFObjectHandle file_spec = pdf_security_marked_dictionary(
        pdf, "embedded-file-spec");
    file_spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
    file_spec.replaceKey(
        "/EF", QPDFObjectHandle::newDictionary({{"/F", embedded_stream}}));
    QPDFObjectHandle embedded_names = pdf_security_marked_dictionary(
        pdf, "embedded-names");
    QPDFObjectHandle name_pairs = QPDFObjectHandle::newArray();
    name_pairs.appendItem(QPDFObjectHandle::newString("payload.bin"));
    name_pairs.appendItem(file_spec);
    embedded_names.replaceKey("/Names", name_pairs);
    root.replaceKey(
        "/Names", QPDFObjectHandle::newDictionary({
            {"/JavaScript", javascript_names},
            {"/EmbeddedFiles", embedded_names},
            {"/Dests", QPDFObjectHandle::newDictionary()}}));

    QPDFObjectHandle af_spec = pdf_security_marked_dictionary(
        pdf, "embedded-af-file-spec");
    af_spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
    QPDFObjectHandle associated_files = QPDFObjectHandle::newArray();
    associated_files.appendItem(af_spec);
    root.replaceKey("/AF", associated_files);

    QPDFObjectHandle ef_stream = pdf.newStream("embedded-ef-payload");
    ef_stream.getDict().replaceKey(
        "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    ef_stream.getDict().replaceKey(
        "/QuantaPDFMarker", QPDFObjectHandle::newString("embedded-ef-stream"));
    root.replaceKey(
        "/QuantaPDFEFOwner",
        QPDFObjectHandle::newDictionary({
            {"/EF", QPDFObjectHandle::newDictionary({{"/F", ef_stream}})}}));

    QPDFObjectHandle xfa = pdf.newStream("xfa-payload");
    xfa.getDict().replaceKey(
        "/QuantaPDFMarker", QPDFObjectHandle::newString("xfa"));
    root.replaceKey(
        "/AcroForm", QPDFObjectHandle::newDictionary({
            {"/Fields", QPDFObjectHandle::newArray()}, {"/XFA", xfa}}));

    QPDFObjectHandle file_attachment = pdf_security_marked_dictionary(
        pdf, "embedded-file-attachment");
    file_attachment.replaceKey(
        "/Subtype", QPDFObjectHandle::newName("/FileAttachment"));
    QPDFObjectHandle rich_media = pdf_security_marked_dictionary(
        pdf, "rich-media");
    rich_media.replaceKey(
        "/Subtype", QPDFObjectHandle::newName("/RichMedia"));
    QPDFObjectHandle harmless = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({
            {"/Subtype", QPDFObjectHandle::newName("/Text")},
            {"/Type", QPDFObjectHandle::newName("/Filespec")}}));
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(file_attachment);
    annots.appendItem(harmless);
    annots.appendItem(rich_media);
    page.replaceKey("/Annots", annots);
}

static void pdf_security_write(
    QPDF& pdf,
    const char *output_path,
    bool preserve_unreferenced = false,
    bool compressed_objects = false)
{
    QPDFWriter writer(pdf, output_path);
    writer.setDeterministicID(true);
    writer.setObjectStreamMode(
        compressed_objects ? qpdf_o_generate : qpdf_o_disable);
    writer.setStreamDataMode(qpdf_s_preserve);
    writer.setCompressStreams(true);
    writer.setPreserveUnreferencedObjects(preserve_unreferenced);
    writer.write();
}

static void pdf_security_set_open_action(
    QPDF& pdf,
    QPDFObjectHandle root,
    const char *name)
{
    root.replaceKey("/OpenAction", pdf_security_action(pdf, name));
}

static void pdf_security_set_a_action(
    QPDF& pdf,
    QPDFObjectHandle root,
    const char *name)
{
    root.replaceKey(
        "/QuantaPDFActionOwner",
        QPDFObjectHandle::newDictionary({
            {"/A", pdf_security_action(pdf, name)}}));
}

static void pdf_security_set_annotation(
    QPDFObjectHandle page,
    const char *subtype)
{
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    annots.appendItem(QPDFObjectHandle::newDictionary({
        {"/Subtype", QPDFObjectHandle::newName(subtype)}}));
    page.replaceKey("/Annots", annots);
}

static void pdf_security_set_fields(
    QPDFObjectHandle root,
    QPDFObjectHandle field)
{
    QPDFObjectHandle fields = QPDFObjectHandle::newArray();
    fields.appendItem(field);
    root.replaceKey(
        "/AcroForm",
        QPDFObjectHandle::newDictionary({{"/Fields", fields}}));
}

static void pdf_security_set_perms_signature(
    QPDF& pdf,
    QPDFObjectHandle root,
    const char *key)
{
    root.replaceKey(
        "/Perms",
        QPDFObjectHandle::newDictionary({
            {key, pdf_security_signature(pdf)}}));
}

static void pdf_security_stress_shared_next(
    QPDF& pdf,
    QPDFObjectHandle root,
    int count)
{
    std::vector<QPDFObjectHandle> actions;
    actions.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
        actions.push_back(pdf_security_action(pdf, "/GoTo"));

    QPDFObjectHandle next = QPDFObjectHandle::newArray();
    for (int index = 0; index < count; ++index)
        next.appendItem(actions[0]);
    next = pdf.makeIndirectObject(next);

    QPDFObjectHandle owners = QPDFObjectHandle::newArray();
    for (QPDFObjectHandle action : actions) {
        action.replaceKey("/Next", next);
        owners.appendItem(QPDFObjectHandle::newDictionary({
            {"/A", action}}));
    }
    root.replaceKey(
        "/QuantaPDFStressOwners", pdf.makeIndirectObject(owners));
}

static void pdf_security_stress_shared_aa(
    QPDF& pdf,
    QPDFObjectHandle root,
    int count)
{
    QPDFObjectHandle action = pdf_security_action(pdf, "/GoTo");
    QPDFObjectHandle additional = QPDFObjectHandle::newDictionary();
    for (int index = 0; index < count; ++index) {
        additional.replaceKey(
            "/E" + std::to_string(index), action);
    }
    additional = pdf.makeIndirectObject(additional);

    QPDFObjectHandle owners = QPDFObjectHandle::newArray();
    for (int index = 0; index < count; ++index) {
        owners.appendItem(QPDFObjectHandle::newDictionary({
            {"/AA", additional}}));
    }
    root.replaceKey(
        "/QuantaPDFStressOwners", pdf.makeIndirectObject(owners));
}

static void pdf_security_stress_shared_annots(
    QPDF& pdf,
    QPDFObjectHandle root,
    int count)
{
    QPDFObjectHandle annotation = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({
            {"/Subtype", QPDFObjectHandle::newName("/Text")}}));
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    for (int index = 0; index < count; ++index)
        annots.appendItem(annotation);
    annots = pdf.makeIndirectObject(annots);

    QPDFObjectHandle owners = QPDFObjectHandle::newArray();
    for (int index = 0; index < count; ++index) {
        owners.appendItem(QPDFObjectHandle::newDictionary({
            {"/Annots", annots}}));
    }
    root.replaceKey(
        "/QuantaPDFStressOwners", pdf.makeIndirectObject(owners));
}

static void pdf_security_empty_container_fixture(
    QPDF& pdf,
    QPDFObjectHandle root,
    bool selected_children)
{
    QPDFObjectHandle names = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle additional = QPDFObjectHandle::newDictionary();
    QPDFObjectHandle safe = pdf_security_action(pdf, "/GoTo");
    QPDFObjectHandle next = QPDFObjectHandle::newArray();
    if (selected_children) {
        names.replaceKey(
            "/JavaScript", pdf.makeIndirectObject(
                QPDFObjectHandle::newDictionary()));
        additional.replaceKey(
            "/E", pdf_security_action(pdf, "/JavaScript"));
        next.appendItem(pdf_security_action(pdf, "/JavaScript"));
    }
    safe.replaceKey("/Next", next);
    root.replaceKey("/Names", names);
    root.replaceKey(
        "/QuantaPDFEmptyAAOwner",
        QPDFObjectHandle::newDictionary({{"/AA", additional}}));
    root.replaceKey(
        "/QuantaPDFEmptyNextOwner",
        QPDFObjectHandle::newDictionary({{"/A", safe}}));
}

static void pdf_security_shared_selected_aa_fixture(
    QPDF& pdf,
    QPDFObjectHandle root)
{
    QPDFObjectHandle additional = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({
            {"/E", pdf_security_action(pdf, "/JavaScript")}}));
    QPDFObjectHandle owners = QPDFObjectHandle::newArray();
    for (int index = 0; index < 2; ++index) {
        owners.appendItem(QPDFObjectHandle::newDictionary({
            {"/AA", additional}}));
    }
    root.replaceKey("/QuantaPDFSharedAAOwners", owners);
}

static void pdf_security_shared_selected_next_fixture(
    QPDF& pdf,
    QPDFObjectHandle root)
{
    QPDFObjectHandle next = QPDFObjectHandle::newArray();
    next.appendItem(pdf_security_action(pdf, "/JavaScript"));
    next = pdf.makeIndirectObject(next);
    QPDFObjectHandle owners = QPDFObjectHandle::newArray();
    for (int index = 0; index < 2; ++index) {
        QPDFObjectHandle safe = pdf_security_action(pdf, "/GoTo");
        safe.replaceKey("/Next", next);
        owners.appendItem(QPDFObjectHandle::newDictionary({
            {"/A", safe}}));
    }
    root.replaceKey("/QuantaPDFSharedNextOwners", owners);
}

static void pdf_security_javascript_name_tree_fixture(
    QPDF& pdf,
    QPDFObjectHandle root,
    char const *head_name,
    char const *next_name)
{
    QPDFObjectHandle head = pdf_security_marked_action(
        pdf, head_name, head_name == std::string("/GoTo") ?
            "safe-goto-name-tree" : "launch-name-tree");
    if (next_name != nullptr) {
        char const *marker = "other-action";
        if (std::string(next_name) == "/JavaScript")
            marker = "javascript-name-tree-next";
        else if (std::string(next_name) == "/Launch")
            marker = "launch-name-tree-next";
        else if (std::string(next_name) == "/URI")
            marker = "external-action";
        QPDFObjectHandle continuation =
            pdf_security_marked_action(pdf, next_name, marker);
        if (std::string(next_name) == "/JavaScript") {
            continuation.replaceKey(
                "/Next", pdf_security_marked_action(
                    pdf, "/Launch", "launch-name-tree-after-javascript"));
        }
        head.replaceKey("/Next", continuation);
    }
    QPDFObjectHandle names = QPDFObjectHandle::newArray();
    names.appendItem(QPDFObjectHandle::newString("entry"));
    names.appendItem(head);
    QPDFObjectHandle leaf = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({{"/Names", names}}));
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    kids.appendItem(leaf);
    QPDFObjectHandle tree = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({{"/Kids", kids}}));
    root.replaceKey(
        "/Names", QPDFObjectHandle::newDictionary({
            {"/JavaScript", tree}}));
}

static void pdf_security_javascript_name_tree_budget(
    QPDF& pdf,
    QPDFObjectHandle root,
    int count)
{
    std::vector<QPDFObjectHandle> actions;
    actions.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index)
        actions.push_back(pdf_security_action(pdf, "/GoTo"));
    QPDFObjectHandle shared_next = QPDFObjectHandle::newArray();
    for (QPDFObjectHandle action : actions)
        shared_next.appendItem(action);
    shared_next = pdf.makeIndirectObject(shared_next);
    for (QPDFObjectHandle action : actions)
        action.replaceKey("/Next", shared_next);
    QPDFObjectHandle pairs = QPDFObjectHandle::newArray();
    pairs.appendItem(QPDFObjectHandle::newString("entry"));
    pairs.appendItem(actions[0]);
    QPDFObjectHandle tree = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({{"/Names", pairs}}));
    root.replaceKey(
        "/Names", QPDFObjectHandle::newDictionary({
            {"/JavaScript", tree}}));
}

static void pdf_security_set_custom_alias(
    QPDFObjectHandle root,
    QPDFObjectHandle object)
{
    root.replaceKey("/QuantaPDFAlias", object);
}

static QPDFObjectHandle pdf_security_name_tree_leaf(
    QPDF& pdf,
    std::initializer_list<char const *> keys)
{
    QPDFObjectHandle names = QPDFObjectHandle::newArray();
    for (char const *key : keys) {
        names.appendItem(QPDFObjectHandle::newString(key));
        names.appendItem(pdf_security_action(pdf, "/GoTo"));
    }
    return pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary({{"/Names", names}}));
}

static void pdf_security_set_name_tree_limits(
    QPDFObjectHandle node,
    char const *first,
    char const *last)
{
    QPDFObjectHandle limits = QPDFObjectHandle::newArray();
    limits.appendItem(QPDFObjectHandle::newString(first));
    limits.appendItem(QPDFObjectHandle::newString(last));
    node.replaceKey("/Limits", limits);
}

static void pdf_security_budget_tuner(
    QPDF& pdf,
    QPDFObjectHandle root,
    int count)
{
    QPDFObjectHandle shared = pdf.makeIndirectObject(
        QPDFObjectHandle::newDictionary());
    QPDFObjectHandle references = QPDFObjectHandle::newArray();
    for (int index = 0; index < count; ++index)
        references.appendItem(shared);
    root.replaceKey(
        "/QuantaPDFBudgetTuner", pdf.makeIndirectObject(references));
}

extern "C" int pdf_security_create_fixture(
    const char *source_path,
    const char *output_path,
    const char *scenario_utf8)
{
    try {
        std::string const scenario = scenario_utf8;
        auto pdf = QPDF::create();
        pdf->processFile(source_path);
        QPDFObjectHandle root = pdf->getRoot();
        auto pages = pdf->getAllPages();
        bool preserve_unreferenced = false;
        bool compressed_objects = false;
        if (pages.empty())
            return 0;

        if (scenario == "sanitize_combined") {
            pdf_security_make_combined_fixture(*pdf, root, pages[0]);
        } else if (scenario == "sanitize_empty_unselected") {
            pdf_security_empty_container_fixture(*pdf, root, false);
        } else if (scenario == "sanitize_selected_to_empty") {
            pdf_security_empty_container_fixture(*pdf, root, true);
        } else if (scenario == "sanitize_shared_selected_aa") {
            pdf_security_shared_selected_aa_fixture(*pdf, root);
        } else if (scenario == "sanitize_shared_selected_next") {
            pdf_security_shared_selected_next_fixture(*pdf, root);
        } else if (scenario == "name_tree_safe") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/GoTo", nullptr);
        } else if (scenario == "name_tree_head_launch") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/Launch", nullptr);
        } else if (scenario == "name_tree_next_javascript") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/GoTo", "/JavaScript");
        } else if (scenario == "name_tree_next_launch") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/GoTo", "/Launch");
        } else if (scenario == "name_tree_next_external") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/GoTo", "/URI");
        } else if (scenario == "name_tree_next_other") {
            pdf_security_javascript_name_tree_fixture(
                *pdf, root, "/GoTo", "/Named");
        } else if (scenario == "name_tree_valid_multilevel") {
            QPDFObjectHandle left = pdf_security_name_tree_leaf(
                *pdf, {"alpha", "bravo"});
            QPDFObjectHandle right = pdf_security_name_tree_leaf(
                *pdf, {"charlie", "delta"});
            pdf_security_set_name_tree_limits(left, "alpha", "bravo");
            pdf_security_set_name_tree_limits(right, "charlie", "delta");
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(left);
            kids.appendItem(right);
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Kids", kids}}));
            pdf_security_set_name_tree_limits(tree, "alpha", "delta");
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
        } else if (scenario == "name_tree_limited_head_launch") {
            QPDFObjectHandle left_names = QPDFObjectHandle::newArray();
            left_names.appendItem(QPDFObjectHandle::newString("alpha"));
            left_names.appendItem(pdf_security_action(*pdf, "/Launch"));
            left_names.appendItem(QPDFObjectHandle::newString("bravo"));
            left_names.appendItem(pdf_security_action(*pdf, "/GoTo"));
            QPDFObjectHandle left = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Names", left_names}}));
            QPDFObjectHandle right = pdf_security_name_tree_leaf(
                *pdf, {"charlie", "delta"});
            pdf_security_set_name_tree_limits(left, "alpha", "bravo");
            pdf_security_set_name_tree_limits(right, "charlie", "delta");
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(left);
            kids.appendItem(right);
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Kids", kids}}));
            pdf_security_set_name_tree_limits(tree, "alpha", "delta");
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
        } else if (scenario == "internal_goto") {
            pdf_security_set_open_action(*pdf, root, "/GoTo");
        } else if (scenario == "open_destination_array") {
            root.replaceKey("/OpenAction", QPDFObjectHandle::newArray());
        } else if (scenario == "open_destination_name") {
            root.replaceKey(
                "/OpenAction", QPDFObjectHandle::newName("/Destination"));
        } else if (scenario == "open_destination_string") {
            root.replaceKey(
                "/OpenAction", QPDFObjectHandle::newString("destination"));
        } else if (scenario == "action_javascript") {
            pdf_security_set_open_action(*pdf, root, "/JavaScript");
        } else if (scenario == "names_javascript") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary()}}));
        } else if (scenario == "action_launch") {
            pdf_security_set_a_action(*pdf, root, "/Launch");
        } else if (scenario == "external_uri") {
            QPDFObjectHandle additional = QPDFObjectHandle::newDictionary({
                {"/E", pdf_security_action(*pdf, "/URI")}});
            root.replaceKey(
                "/QuantaPDFAdditionalOwner",
                QPDFObjectHandle::newDictionary({{"/AA", additional}}));
        } else if (scenario == "external_gotor") {
            pdf_security_set_open_action(*pdf, root, "/GoToR");
        } else if (scenario == "external_gotoe") {
            pdf_security_set_open_action(*pdf, root, "/GoToE");
        } else if (scenario == "external_submitform") {
            pdf_security_set_open_action(*pdf, root, "/SubmitForm");
        } else if (scenario == "external_importdata") {
            pdf_security_set_open_action(*pdf, root, "/ImportData");
        } else if (scenario == "other_unknown") {
            pdf_security_set_open_action(*pdf, root, "/QuantaPDFUnknown");
        } else if (scenario == "goto_next_other") {
            QPDFObjectHandle action = pdf_security_action(*pdf, "/GoTo");
            QPDFObjectHandle next = QPDFObjectHandle::newArray();
            next.appendItem(pdf_security_action(*pdf, "/Named"));
            action.replaceKey("/Next", next);
            root.replaceKey("/OpenAction", action);
        } else if (scenario == "names_embedded") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/EmbeddedFiles", QPDFObjectHandle::newDictionary()}}));
        } else if (scenario == "af_embedded") {
            root.replaceKey(
                "/AF", pdf->makeIndirectObject(
                    QPDFObjectHandle::newDictionary()));
        } else if (scenario == "ef_embedded") {
            root.replaceKey(
                "/QuantaPDFFileSpec",
                QPDFObjectHandle::newDictionary({
                    {"/EF", pdf->makeIndirectObject(
                        QPDFObjectHandle::newDictionary())}}));
        } else if (scenario == "embedded_stream") {
            QPDFObjectHandle embedded = pdf->newStream("embedded");
            embedded.getDict().replaceKey(
                "/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            root.replaceKey("/QuantaPDFEmbedded", embedded);
        } else if (scenario == "file_attachment") {
            pdf_security_set_annotation(pages[0], "/FileAttachment");
        } else if (scenario == "xfa") {
            root.replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", QPDFObjectHandle::newArray()},
                    {"/XFA", QPDFObjectHandle::newString("xfa")}}));
        } else if (scenario == "rich_richmedia") {
            pdf_security_set_annotation(pages[0], "/RichMedia");
        } else if (scenario == "rich_3d") {
            pdf_security_set_annotation(pages[0], "/3D");
        } else if (scenario == "rich_movie") {
            pdf_security_set_annotation(pages[0], "/Movie");
        } else if (scenario == "rich_sound") {
            pdf_security_set_annotation(pages[0], "/Sound");
        } else if (scenario == "rich_screen") {
            pdf_security_set_annotation(pages[0], "/Screen");
        } else if (scenario == "inherited_signature") {
            QPDFObjectHandle parent = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary());
            QPDFObjectHandle child = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Parent", parent},
                    {"/V", pdf_security_signature(*pdf)}}));
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(child);
            parent.replaceKey("/FT", QPDFObjectHandle::newName("/Sig"));
            parent.replaceKey("/Kids", kids);
            pdf_security_set_fields(root, parent);
        } else if (scenario == "perms_docmdp") {
            pdf_security_set_perms_signature(*pdf, root, "/DocMDP");
        } else if (scenario == "perms_ur") {
            pdf_security_set_perms_signature(*pdf, root, "/UR");
        } else if (scenario == "perms_ur3") {
            pdf_security_set_perms_signature(*pdf, root, "/UR3");
        } else if (scenario == "unreachable_javascript") {
            QPDFObjectHandle garbage = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/A", pdf_security_action(*pdf, "/JavaScript")}}));
            (void)garbage;
            preserve_unreferenced = true;
        } else if (scenario == "nonroot_names") {
            root.replaceKey(
                "/QuantaPDFNonRoot", QPDFObjectHandle::newDictionary({
                    {"/Names", QPDFObjectHandle::newDictionary({
                        {"/JavaScript",
                         QPDFObjectHandle::newDictionary()}})}}));
        } else if (scenario == "nonroot_acroform") {
            root.replaceKey(
                "/QuantaPDFNonRoot", QPDFObjectHandle::newDictionary({
                    {"/AcroForm", QPDFObjectHandle::newDictionary({
                        {"/XFA", QPDFObjectHandle::newString("xfa")}})}}));
        } else if (scenario == "action_cycle") {
            QPDFObjectHandle left = pdf_security_action(*pdf, "/GoTo");
            QPDFObjectHandle right = pdf_security_action(*pdf, "/GoTo");
            left.replaceKey("/Next", right);
            right.replaceKey("/Next", left);
            root.replaceKey("/OpenAction", left);
        } else if (scenario == "malformed_open_action") {
            root.replaceKey(
                "/OpenAction", QPDFObjectHandle::newDictionary());
        } else if (scenario == "malformed_a") {
            root.replaceKey(
                "/QuantaPDFMalformed", QPDFObjectHandle::newDictionary({
                    {"/A", QPDFObjectHandle::newName("/Bad")}}));
        } else if (scenario == "malformed_aa_container") {
            root.replaceKey(
                "/QuantaPDFMalformed", QPDFObjectHandle::newDictionary({
                    {"/AA", QPDFObjectHandle::newName("/Bad")}}));
        } else if (scenario == "malformed_aa_entry") {
            root.replaceKey(
                "/QuantaPDFMalformed", QPDFObjectHandle::newDictionary({
                    {"/AA", QPDFObjectHandle::newDictionary({
                        {"/E", QPDFObjectHandle::newDictionary()}})}}));
        } else if (scenario == "malformed_next") {
            QPDFObjectHandle action = pdf_security_action(*pdf, "/GoTo");
            action.replaceKey(
                "/Next", QPDFObjectHandle::newName("/Bad"));
            root.replaceKey("/OpenAction", action);
        } else if (scenario == "malformed_names") {
            root.replaceKey("/Names", QPDFObjectHandle::newName("/Bad"));
        } else if (scenario == "malformed_js_tree_node") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newName("/Bad")}}));
        } else if (scenario == "malformed_js_tree_kids") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Kids", QPDFObjectHandle::newName("/Bad")}})}}));
        } else if (scenario == "malformed_js_tree_kid") {
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(QPDFObjectHandle::newName("/Bad"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Kids", kids}})}}));
        } else if (scenario == "malformed_js_tree_names") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", QPDFObjectHandle::newName("/Bad")}})}}));
        } else if (scenario == "malformed_js_tree_odd_names") {
            QPDFObjectHandle names = QPDFObjectHandle::newArray();
            names.appendItem(QPDFObjectHandle::newString("key"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", names}})}}));
        } else if (scenario == "malformed_js_tree_key") {
            QPDFObjectHandle names = QPDFObjectHandle::newArray();
            names.appendItem(QPDFObjectHandle::newName("/Bad"));
            names.appendItem(pdf_security_action(*pdf, "/GoTo"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", names}})}}));
        } else if (scenario == "malformed_js_tree_value") {
            QPDFObjectHandle names = QPDFObjectHandle::newArray();
            names.appendItem(QPDFObjectHandle::newString("key"));
            names.appendItem(QPDFObjectHandle::newName("/Bad"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", names}})}}));
        } else if (scenario == "malformed_js_tree_action") {
            QPDFObjectHandle names = QPDFObjectHandle::newArray();
            names.appendItem(QPDFObjectHandle::newString("key"));
            names.appendItem(QPDFObjectHandle::newDictionary());
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", names}})}}));
        } else if (scenario == "malformed_js_tree_both") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Kids", QPDFObjectHandle::newArray()},
                        {"/Names", QPDFObjectHandle::newArray()}})}}));
        } else if (scenario == "malformed_js_tree_limits") {
            QPDFObjectHandle limits = QPDFObjectHandle::newArray();
            limits.appendItem(QPDFObjectHandle::newString("only-one"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Limits", limits}})}}));
        } else if (scenario == "name_tree_cycle") {
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary());
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(tree);
            tree.replaceKey("/Kids", kids);
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
        } else if (scenario == "name_tree_duplicate_child") {
            QPDFObjectHandle leaf = pdf_security_name_tree_leaf(*pdf, {"a"});
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(leaf);
            kids.appendItem(leaf);
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Kids", kids}})}}));
        } else if (scenario == "name_tree_unordered_keys" ||
                   scenario == "name_tree_duplicate_keys") {
            QPDFObjectHandle names = QPDFObjectHandle::newArray();
            names.appendItem(QPDFObjectHandle::newString(
                scenario == "name_tree_unordered_keys" ? "b" : "a"));
            names.appendItem(pdf_security_action(*pdf, "/GoTo"));
            names.appendItem(QPDFObjectHandle::newString("a"));
            names.appendItem(pdf_security_action(*pdf, "/GoTo"));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Names", names}})}}));
        } else if (scenario == "name_tree_reversed_limits" ||
                   scenario == "name_tree_inconsistent_limits") {
            QPDFObjectHandle tree = pdf_security_name_tree_leaf(
                *pdf, {"a", "b"});
            pdf_security_set_name_tree_limits(
                tree,
                scenario == "name_tree_reversed_limits" ? "b" : "x",
                scenario == "name_tree_reversed_limits" ? "a" : "z");
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
        } else if (scenario == "name_tree_overlapping_kids" ||
                   scenario == "name_tree_out_of_order_kids") {
            QPDFObjectHandle first = pdf_security_name_tree_leaf(
                *pdf, scenario == "name_tree_overlapping_kids"
                    ? std::initializer_list<char const *>{"a", "c"}
                    : std::initializer_list<char const *>{"c", "d"});
            QPDFObjectHandle second = pdf_security_name_tree_leaf(
                *pdf, scenario == "name_tree_overlapping_kids"
                    ? std::initializer_list<char const *>{"b", "d"}
                    : std::initializer_list<char const *>{"a", "b"});
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(first);
            kids.appendItem(second);
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary({
                        {"/Kids", kids}})}}));
        } else if (scenario == "malformed_annots") {
            pages[0].replaceKey(
                "/Annots", QPDFObjectHandle::newName("/Bad"));
        } else if (scenario == "malformed_annot_entry") {
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newName("/Bad"));
            pages[0].replaceKey("/Annots", annots);
        } else if (scenario == "malformed_acroform") {
            root.replaceKey(
                "/AcroForm", QPDFObjectHandle::newName("/Bad"));
        } else if (scenario == "malformed_fields") {
            root.replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", QPDFObjectHandle::newName("/Bad")}}));
        } else if (scenario == "malformed_field") {
            pdf_security_set_fields(
                root, QPDFObjectHandle::newName("/Bad"));
        } else if (scenario == "malformed_signature_value") {
            pdf_security_set_fields(
                root, pdf->makeIndirectObject(
                    QPDFObjectHandle::newDictionary({
                        {"/FT", QPDFObjectHandle::newName("/Sig")},
                        {"/V", QPDFObjectHandle::newName("/Bad")}})));
        } else if (scenario == "malformed_signature_type") {
            QPDFObjectHandle signature = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Type", QPDFObjectHandle::newName("/Bad")}}));
            pdf_security_set_fields(
                root, pdf->makeIndirectObject(
                    QPDFObjectHandle::newDictionary({
                        {"/FT", QPDFObjectHandle::newName("/Sig")},
                        {"/V", signature}})));
        } else if (scenario == "parent_mismatch") {
            QPDFObjectHandle external_parent = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary());
            pdf_security_set_fields(
                root, pdf->makeIndirectObject(
                    QPDFObjectHandle::newDictionary({
                        {"/Parent", external_parent}})));
        } else if (scenario == "malformed_perms") {
            root.replaceKey("/Perms", QPDFObjectHandle::newName("/Bad"));
        } else if (scenario == "malformed_perms_signature") {
            root.replaceKey(
                "/Perms", QPDFObjectHandle::newDictionary({
                    {"/DocMDP", QPDFObjectHandle::newName("/Bad")}}));
        } else if (scenario == "malformed_perms_type") {
            root.replaceKey(
                "/Perms", QPDFObjectHandle::newDictionary({
                    {"/DocMDP", QPDFObjectHandle::newDictionary({
                        {"/Type", QPDFObjectHandle::newName("/Bad")}})}}));
        } else if (scenario == "budget_shared_next") {
            pdf_security_stress_shared_next(*pdf, root, 2048);
            compressed_objects = true;
        } else if (scenario == "budget_shared_aa") {
            pdf_security_stress_shared_aa(*pdf, root, 2048);
            compressed_objects = true;
        } else if (scenario == "budget_shared_annots") {
            pdf_security_stress_shared_annots(*pdf, root, 2048);
            compressed_objects = true;
        } else if (scenario == "budget_js_name_tree") {
            pdf_security_javascript_name_tree_budget(*pdf, root, 1024);
            compressed_objects = true;
        } else if (scenario == "alias_annots_next") {
            QPDFObjectHandle item = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/S", QPDFObjectHandle::newName("/Launch")},
                    {"/Subtype", QPDFObjectHandle::newName("/RichMedia")}}));
            QPDFObjectHandle shared = QPDFObjectHandle::newArray();
            shared.appendItem(item);
            shared = pdf->makeIndirectObject(shared);
            pages[0].replaceKey("/Annots", shared);
            QPDFObjectHandle safe = pdf_security_action(*pdf, "/GoTo");
            safe.replaceKey("/Next", shared);
            root.replaceKey(
                "/QuantaPDFActionOwner",
                QPDFObjectHandle::newDictionary({{"/A", safe}}));
        } else if (scenario == "alias_names_custom") {
            QPDFObjectHandle names = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary()}}));
            root.replaceKey("/Names", names);
            pdf_security_set_custom_alias(root, names);
        } else if (scenario == "alias_js_names_array_custom") {
            QPDFObjectHandle pairs = QPDFObjectHandle::newArray();
            pairs.appendItem(QPDFObjectHandle::newString("entry"));
            pairs.appendItem(pdf_security_action(*pdf, "/Launch"));
            pairs = pdf->makeIndirectObject(pairs);
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Names", pairs}}));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
            pdf_security_set_custom_alias(root, pairs);
        } else if (scenario == "alias_acroform_custom") {
            QPDFObjectHandle acroform = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/Fields", QPDFObjectHandle::newArray()},
                    {"/XFA", QPDFObjectHandle::newString("xfa")}}));
            root.replaceKey("/AcroForm", acroform);
            pdf_security_set_custom_alias(root, acroform);
        } else if (scenario == "alias_aa_custom") {
            QPDFObjectHandle additional = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/E", pdf_security_action(*pdf, "/Launch")}}));
            root.replaceKey(
                "/QuantaPDFOwner",
                QPDFObjectHandle::newDictionary({{"/AA", additional}}));
            pdf_security_set_custom_alias(root, additional);
        } else if (scenario == "alias_next_custom") {
            QPDFObjectHandle next = QPDFObjectHandle::newArray();
            next.appendItem(pdf_security_action(*pdf, "/Launch"));
            next = pdf->makeIndirectObject(next);
            QPDFObjectHandle safe = pdf_security_action(*pdf, "/GoTo");
            safe.replaceKey("/Next", next);
            root.replaceKey(
                "/QuantaPDFOwner",
                QPDFObjectHandle::newDictionary({{"/A", safe}}));
            pdf_security_set_custom_alias(root, next);
        } else if (scenario == "alias_openaction_next_custom") {
            QPDFObjectHandle safe = pdf_security_action(*pdf, "/GoTo");
            safe.replaceKey(
                "/Next", pdf_security_action(*pdf, "/Launch"));
            root.replaceKey("/OpenAction", safe);
            pdf_security_set_custom_alias(root, safe);
        } else if (scenario == "alias_direct_next_custom") {
            QPDFObjectHandle owner = pdf_security_action(*pdf, "/GoTo");
            QPDFObjectHandle next = QPDFObjectHandle::newArray();
            next.appendItem(pdf_security_action(*pdf, "/Launch"));
            next.appendItem(pdf_security_action(*pdf, "/GoTo"));
            owner.replaceKey("/Next", next);
            root.replaceKey("/OpenAction", owner);
            pdf_security_set_custom_alias(root, owner);
        } else if (scenario == "alias_direct_aa_custom") {
            QPDFObjectHandle owner = pdf_security_action(*pdf, "/GoTo");
            owner.replaceKey(
                "/AA", QPDFObjectHandle::newDictionary({
                    {"/E", pdf_security_action(*pdf, "/Launch")},
                    {"/X", pdf_security_action(*pdf, "/GoTo")}}));
            root.replaceKey("/OpenAction", owner);
            pdf_security_set_custom_alias(root, owner);
        } else if (scenario == "alias_direct_annots_custom") {
            QPDFObjectHandle owner = pdf_security_action(*pdf, "/GoTo");
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/RichMedia")}}));
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/Text")}}));
            owner.replaceKey("/Annots", annots);
            root.replaceKey("/OpenAction", owner);
            pdf_security_set_custom_alias(root, owner);
        } else if (scenario == "alias_direct_page_annots_custom") {
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/RichMedia")}}));
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/Text")}}));
            pages[0].replaceKey("/Annots", annots);
            pdf_security_set_custom_alias(root, pages[0]);
        } else if (scenario == "alias_direct_js_names_custom") {
            QPDFObjectHandle pairs = QPDFObjectHandle::newArray();
            pairs.appendItem(QPDFObjectHandle::newString("a"));
            pairs.appendItem(pdf_security_action(*pdf, "/Launch"));
            pairs.appendItem(QPDFObjectHandle::newString("b"));
            pairs.appendItem(pdf_security_action(*pdf, "/GoTo"));
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Names", pairs}}));
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
            pdf_security_set_custom_alias(root, tree);
        } else if (scenario == "alias_direct_js_limits_custom") {
            QPDFObjectHandle pairs = QPDFObjectHandle::newArray();
            pairs.appendItem(QPDFObjectHandle::newString("alpha"));
            pairs.appendItem(pdf_security_action(*pdf, "/Launch"));
            pairs.appendItem(QPDFObjectHandle::newString("bravo"));
            pairs.appendItem(pdf_security_action(*pdf, "/GoTo"));
            QPDFObjectHandle leaf = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Names", pairs}}));
            pdf_security_set_name_tree_limits(leaf, "alpha", "bravo");
            QPDFObjectHandle kids = QPDFObjectHandle::newArray();
            kids.appendItem(leaf);
            QPDFObjectHandle tree = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({{"/Kids", kids}}));
            pdf_security_set_name_tree_limits(tree, "alpha", "bravo");
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", tree}}));
            pdf_security_set_custom_alias(root, tree);
        } else if (scenario == "alias_direct_catalog_names_custom") {
            root.replaceKey(
                "/Names", QPDFObjectHandle::newDictionary({
                    {"/JavaScript", QPDFObjectHandle::newDictionary()}}));
            pdf_security_set_custom_alias(root, root);
        } else if (scenario == "alias_direct_acroform_custom") {
            root.replaceKey(
                "/AcroForm", QPDFObjectHandle::newDictionary({
                    {"/Fields", QPDFObjectHandle::newArray()},
                    {"/XFA", QPDFObjectHandle::newString("xfa")}}));
            pdf_security_set_custom_alias(root, root);
        } else if (scenario == "alias_direct_af_ef_custom") {
            QPDFObjectHandle owner = pdf_security_action(*pdf, "/GoTo");
            owner.replaceKey(
                "/QuantaPDFChild", QPDFObjectHandle::newDictionary({
                    {"/AF", QPDFObjectHandle::newArray()},
                    {"/EF", QPDFObjectHandle::newDictionary()}}));
            root.replaceKey("/OpenAction", owner);
            pdf_security_set_custom_alias(root, owner);
        } else if (scenario == "alias_direct_action_owner_custom") {
            QPDFObjectHandle owner = pdf_security_action(*pdf, "/GoTo");
            owner.replaceKey(
                "/QuantaPDFChild", QPDFObjectHandle::newDictionary({
                    {"/A", pdf_security_action(*pdf, "/Launch")}}));
            root.replaceKey("/OpenAction", owner);
            pdf_security_set_custom_alias(root, owner);
        } else if (scenario == "alias_annots_custom") {
            QPDFObjectHandle annots = QPDFObjectHandle::newArray();
            annots.appendItem(QPDFObjectHandle::newDictionary({
                {"/Subtype", QPDFObjectHandle::newName("/RichMedia")}}));
            annots = pdf->makeIndirectObject(annots);
            pages[0].replaceKey("/Annots", annots);
            pdf_security_set_custom_alias(root, annots);
        } else if (scenario == "alias_af_custom") {
            QPDFObjectHandle holder = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/AF", QPDFObjectHandle::newArray()}}));
            QPDFObjectHandle associated = QPDFObjectHandle::newArray();
            associated.appendItem(holder);
            root.replaceKey("/AF", associated);
            pdf_security_set_custom_alias(root, holder);
        } else if (scenario == "alias_ef_custom") {
            QPDFObjectHandle holder = pdf->makeIndirectObject(
                QPDFObjectHandle::newDictionary({
                    {"/EF", QPDFObjectHandle::newDictionary()}}));
            root.replaceKey("/EF", holder);
            pdf_security_set_custom_alias(root, holder);
        } else if (scenario == "mutation_budget_shared_next") {
            pdf_security_stress_shared_next(*pdf, root, 590);
            pdf_security_budget_tuner(*pdf, root, 212);
            root.replaceKey("/Names", QPDFObjectHandle::newDictionary());
            root.replaceKey("/AcroForm", QPDFObjectHandle::newDictionary());
            compressed_objects = true;
        } else {
            return 0;
        }

        pdf_security_write(
            *pdf, output_path, preserve_unreferenced, compressed_objects);
        if (scenario == "mutation_budget_shared_next") {
            std::ifstream written(
                output_path, std::ios::binary | std::ios::ate);
            std::streamoff const written_size = written.tellg();
            written.close();
            constexpr std::streamoff target_size = 5498;
            if (written_size < 0 || written_size > target_size)
                return 0;
            std::ofstream padding(
                output_path, std::ios::binary | std::ios::app);
            padding << std::string(
                static_cast<size_t>(target_size - written_size), ' ');
            if (!padding)
                return 0;
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" int pdf_security_inspect_empty_containers(
    unsigned char const *data,
    size_t size)
{
    if (data == nullptr || size == 0)
        return -1;
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "pdf-security-empty-container-output",
            reinterpret_cast<char const *>(data),
            size);
        QPDFObjectHandle root = pdf->getRoot();
        int mask = 0;
        QPDFObjectHandle names = root.getKey("/Names");
        if (names.isDictionary() && names.getKeys().empty())
            mask |= 1;
        QPDFObjectHandle aa_owner = root.getKey("/QuantaPDFEmptyAAOwner");
        QPDFObjectHandle additional = aa_owner.getKey("/AA");
        if (additional.isDictionary() && additional.getKeys().empty())
            mask |= 2;
        QPDFObjectHandle next_owner =
            root.getKey("/QuantaPDFEmptyNextOwner");
        QPDFObjectHandle action = next_owner.getKey("/A");
        QPDFObjectHandle next = action.getKey("/Next");
        if (next.isArray() && next.getArrayNItems() == 0)
            mask |= 4;
        return pdf->anyWarnings() ? -1 : mask;
    } catch (...) {
        return -1;
    }
}

extern "C" int pdf_security_inspect_shared_container_owners(
    unsigned char const *data,
    size_t size,
    int inspect_next)
{
    if (data == nullptr || size == 0)
        return -1;
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "pdf-security-shared-container-output",
            reinterpret_cast<char const *>(data),
            size);
        QPDFObjectHandle root = pdf->getRoot();
        QPDFObjectHandle owners = root.getKey(
            inspect_next ? "/QuantaPDFSharedNextOwners" :
                           "/QuantaPDFSharedAAOwners");
        if (!owners.isArray() || owners.getArrayNItems() != 2)
            return -1;
        int mask = 0;
        for (int index = 0; index < 2; ++index) {
            QPDFObjectHandle owner = owners.getArrayItem(index);
            if (!owner.isDictionary())
                return -1;
            if (inspect_next) {
                QPDFObjectHandle action = owner.getKey("/A");
                if (!action.isDictionary())
                    return -1;
                if (action.hasKey("/Next"))
                    mask |= 1 << index;
            } else if (owner.hasKey("/AA")) {
                mask |= 1 << index;
            }
        }
        return pdf->anyWarnings() ? -1 : mask;
    } catch (...) {
        return -1;
    }
}

extern "C" int pdf_security_inspect_output(
    unsigned char const *data,
    size_t size,
    uint32_t *out_markers,
    int *out_has_object_stream)
{
    if (data == nullptr || size == 0 || out_markers == nullptr ||
        out_has_object_stream == nullptr)
        return 0;
    *out_markers = 0;
    *out_has_object_stream = 0;
    try {
        auto pdf = QPDF::create();
        pdf->setSuppressWarnings(true);
        pdf->setAttemptRecovery(false);
        pdf->processMemoryFile(
            "pdf-security-sanitize-output",
            reinterpret_cast<char const *>(data),
            size);
        for (QPDFObjectHandle object : pdf->getAllObjects()) {
            QPDFObjectHandle dictionary = object.isStream()
                ? object.getDict()
                : object;
            if (!dictionary.isDictionary())
                continue;
            QPDFObjectHandle type = dictionary.getKey("/Type");
            if (type.isName() && type.getName() == "/ObjStm")
                *out_has_object_stream = 1;
            QPDFObjectHandle marker = dictionary.getKey("/QuantaPDFMarker");
            if (!marker.isString())
                continue;
            std::string const value = marker.getUTF8Value();
            if (value.rfind("javascript-", 0) == 0) {
                *out_markers |= pdf_security_marker_javascript;
            } else if (value.rfind("launch-", 0) == 0) {
                *out_markers |= pdf_security_marker_launch;
            } else if (value == "external-action") {
                *out_markers |= pdf_security_marker_external;
            } else if (value == "other-action") {
                *out_markers |= pdf_security_marker_other;
            } else if (value.rfind("embedded-", 0) == 0) {
                *out_markers |= pdf_security_marker_embedded;
            } else if (value == "xfa") {
                *out_markers |= pdf_security_marker_xfa;
            } else if (value == "rich-media") {
                *out_markers |= pdf_security_marker_rich_media;
            } else if (value.rfind("safe-goto", 0) == 0) {
                *out_markers |= pdf_security_marker_safe_goto;
            }
        }
        if (pdf->anyWarnings())
            return 0;
        return 1;
    } catch (...) {
        return 0;
    }
}
