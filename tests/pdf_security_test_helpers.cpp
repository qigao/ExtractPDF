#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <cstdint>
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
        } else {
            return 0;
        }

        pdf_security_write(
            *pdf, output_path, preserve_unreferenced, compressed_objects);
        return 1;
    } catch (...) {
        return 0;
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
