/*  Copyright 2011, 2012 The Ready Bunch

    This file is part of Ready.

    Ready is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ready is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ready. If not, see <http://www.gnu.org/licenses/>.         */

// local:
#include "app.hpp"              // for wxGetApp
#include "frame.hpp"
#include "vtk_pipeline.hpp"
#include "utils.hpp"
#include "wxutils.hpp"
#include "prefs.hpp"            // for GetPrefs, SavePrefs, etc
#include "IO_XML.hpp"
#include "RulePanel.hpp"
#include "HelpPanel.hpp"
#include "PatternsPanel.hpp"
#include "IDs.hpp"

// readybase:
#include "GrayScott.hpp"
#include "OpenCL_nDim.hpp"

// local resources:
#include "appicon16.xpm"

// wxWidgets:
#include <wx/config.h>
#include <wx/aboutdlg.h>
#include <wx/filename.h>
#include <wx/font.h>
#include <wx/dir.h>
#include <wx/dnd.h>             // for wxFileDropTarget
#include <wx/artprov.h>
#if wxUSE_TOOLTIPS
   #include "wx/tooltip.h"      // for wxToolTip
#endif

// wxVTK: (local copy)
#include "wxVTKRenderWindowInteractor.h"

// STL:
#include <fstream>
#include <string>
#include <stdexcept>
using namespace std;

// VTK:
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkJPEGWriter.h>
#include <vtkSmartPointer.h>

#ifdef __WXMAC__
    #include <Carbon/Carbon.h>  // for GetCurrentProcess, etc
#endif

#if wxCHECK_VERSION(2,9,0)
    // some wxMenuItem method names have changed in wx 2.9
    #define GetText GetItemLabel
    #define SetText SetItemLabel
#endif

wxString PaneName(int id)
{
    return wxString::Format(_T("%d"),id);
}

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_IDLE(MyFrame::OnIdle)
    EVT_SIZE(MyFrame::OnSize)
    EVT_CLOSE(MyFrame::OnClose)
    // file menu
    EVT_MENU(wxID_NEW, MyFrame::OnNewPattern)
    EVT_MENU(wxID_OPEN, MyFrame::OnOpenPattern)
    EVT_MENU(wxID_SAVE, MyFrame::OnSavePattern)
    EVT_MENU(ID::Screenshot, MyFrame::OnScreenshot)
    EVT_MENU(wxID_PREFERENCES, MyFrame::OnPreferences)
    EVT_MENU(wxID_EXIT, MyFrame::OnQuit)
    // edit menu
    EVT_MENU(wxID_CUT, MyFrame::OnCut)
    EVT_MENU(wxID_COPY, MyFrame::OnCopy)
    EVT_MENU(wxID_PASTE, MyFrame::OnPaste)
    EVT_UPDATE_UI(wxID_PASTE, MyFrame::OnUpdatePaste)
    EVT_MENU(wxID_CLEAR, MyFrame::OnClear)
    EVT_MENU(wxID_SELECTALL, MyFrame::OnSelectAll)
    // view menu
    EVT_MENU(ID::FullScreen, MyFrame::OnFullScreen)
    EVT_MENU(ID::FitPattern, MyFrame::OnFitPattern)
    EVT_MENU(ID::Wireframe, MyFrame::OnWireframe)
    EVT_UPDATE_UI(ID::Wireframe, MyFrame::OnUpdateWireframe)
    EVT_MENU(ID::PatternsPane, MyFrame::OnToggleViewPane)
    EVT_UPDATE_UI(ID::PatternsPane, MyFrame::OnUpdateViewPane)
    EVT_MENU(ID::RulePane, MyFrame::OnToggleViewPane)
    EVT_UPDATE_UI(ID::RulePane, MyFrame::OnUpdateViewPane)
    EVT_MENU(ID::HelpPane, MyFrame::OnToggleViewPane)
    EVT_UPDATE_UI(ID::HelpPane, MyFrame::OnUpdateViewPane)
    EVT_MENU(ID::RestoreDefaultPerspective, MyFrame::OnRestoreDefaultPerspective)
    EVT_MENU(ID::ChangeActiveChemical, MyFrame::OnChangeActiveChemical)
    // action menu
    EVT_MENU(ID::Step, MyFrame::OnStep)
    EVT_UPDATE_UI(ID::Step, MyFrame::OnUpdateStep)
    EVT_MENU(ID::RunStop, MyFrame::OnRunStop)
    EVT_UPDATE_UI(ID::RunStop, MyFrame::OnUpdateRunStop)
    EVT_MENU(ID::Reset, MyFrame::OnReset)
    EVT_UPDATE_UI(ID::Reset, MyFrame::OnUpdateReset)
    EVT_MENU(ID::GenerateInitialPattern, MyFrame::OnGenerateInitialPattern)
    EVT_MENU(ID::SelectOpenCLDevice, MyFrame::OnSelectOpenCLDevice)
    EVT_MENU(ID::OpenCLDiagnostics, MyFrame::OnOpenCLDiagnostics)
    // help menu
    EVT_MENU(wxID_HELP, MyFrame::OnHelp)
    EVT_MENU(ID::HelpQuick, MyFrame::OnHelp)
    EVT_MENU(ID::HelpTips, MyFrame::OnHelp)
    EVT_MENU(ID::HelpKeyboard, MyFrame::OnHelp)
    EVT_MENU(ID::HelpMouse, MyFrame::OnHelp)
    EVT_MENU(ID::HelpFile, MyFrame::OnHelp)
    EVT_MENU(ID::HelpEdit, MyFrame::OnHelp)
    EVT_MENU(ID::HelpView, MyFrame::OnHelp)
    EVT_MENU(ID::HelpAction, MyFrame::OnHelp)
    EVT_MENU(ID::HelpHelp, MyFrame::OnHelp)
    EVT_MENU(ID::HelpRefs, MyFrame::OnHelp)
    EVT_MENU(ID::HelpFormats, MyFrame::OnHelp)
    EVT_MENU(ID::HelpProblems, MyFrame::OnHelp)
    EVT_MENU(ID::HelpChanges, MyFrame::OnHelp)
    EVT_MENU(ID::HelpCredits, MyFrame::OnHelp)
    EVT_MENU(wxID_ABOUT, MyFrame::OnAbout)
    // items in Open Recent submenu must be handled last
    EVT_MENU(wxID_ANY, MyFrame::OnOpenRecent)
END_EVENT_TABLE()

// frame constructor
MyFrame::MyFrame(const wxString& title)
       : wxFrame(NULL, wxID_ANY, title),
       pVTKWindow(NULL),system(NULL),
       is_running(false),
       is_wireframe(false),
       timesteps_per_render(100),
       frames_per_second(0.0),
       million_cell_generations_per_second(0.0),
       iActiveChemical(1),
       fullscreen(false)
{
    this->SetIcon(wxICON(appicon16));
    #ifdef __WXGTK__
        // advanced docking hints cause problems on xfce (and probably others)
        this->aui_mgr.SetFlags( wxAUI_MGR_ALLOW_FLOATING | wxAUI_MGR_RECTANGLE_HINT );
    #endif
    this->aui_mgr.SetManagedWindow(this);
    
    GetPrefs();     // must be called before InitializeMenus

    this->InitializeMenus();
    this->InitializeToolbars();

    CreateStatusBar(1);
    SetStatusText(_("Ready"));

    this->InitializePatternsPane();
    this->InitializeRulePane();
    this->InitializeHelpPane();
    this->InitializeRenderPane();
    
    this->default_perspective = this->aui_mgr.SavePerspective();
    this->LoadSettings();
    this->aui_mgr.Update();

    // enable/disable tool tips
    #if wxUSE_TOOLTIPS
        // AKT TODO!!! fix bug: not seeing any tips in Mac app (bug in wxAUI???)
        wxToolTip::Enable(showtips);
        #ifdef __WXMAC__
            wxToolTip::SetDelay(1500);  // 1.5 secs is better on Mac
        #endif
    #endif

    // initialize an RD system to get us started
    const wxString initfile = _T("Patterns/CPU-only/grayscott_3D.vti");
    if (wxFileExists(initfile)) {
        this->OpenFile(initfile);
    } else {
        // create new pattern
        wxCommandEvent cmdevent(wxID_NEW);
        OnNewPattern(cmdevent);
    }

    this->starting_pattern = vtkImageData::New();
}

void MyFrame::InitializeMenus()
{
    wxMenuBar *menuBar = new wxMenuBar();
    {   // file menu:
        wxMenu *menu = new wxMenu;
        menu->Append(wxID_NEW, _("New Pattern") + GetAccelerator(DO_NEWPATT), _("Create a new pattern"));
        menu->AppendSeparator();
        menu->Append(wxID_OPEN, _("Open Pattern...") + GetAccelerator(DO_OPENPATT), _("Choose a pattern file to open"));
        menu->Append(ID::OpenRecent, _("Open Recent"), patternSubMenu);
        menu->AppendSeparator();
        menu->Append(wxID_SAVE, _("Save Pattern...") + GetAccelerator(DO_SAVE), _("Save the current pattern"));
        menu->Append(ID::Screenshot, _("Save Screenshot...") + GetAccelerator(DO_SCREENSHOT), _("Save a screenshot of the current view"));
        #if !defined(__WXOSX_COCOA__)
            menu->AppendSeparator();
        #endif
        // on the Mac the wxID_PREFERENCES item is moved to the app menu
        menu->Append(wxID_PREFERENCES, _("Preferences...") + GetAccelerator(DO_PREFS), _("Edit the preferences"));
        #if !defined(__WXOSX_COCOA__)
            menu->AppendSeparator();
        #endif
        // on the Mac the wxID_EXIT item is moved to the app menu and the app name is appended to "Quit "
        menu->Append(wxID_EXIT, _("Quit") + GetAccelerator(DO_QUIT));
        menuBar->Append(menu, _("&File"));
    }
    {   // edit menu:
        wxMenu *menu = new wxMenu;
        menu->Append(wxID_CUT, _("Cut") + GetAccelerator(DO_CUT), _("Cut the selection and save it to the clipboard"));
        menu->Append(wxID_COPY, _("Copy") + GetAccelerator(DO_COPY), _("Copy the selection to the clipboard"));
        menu->Append(wxID_PASTE, _("Paste") + GetAccelerator(DO_PASTE), _("Paste in the contents of the clipboard"));
        menu->Append(wxID_CLEAR, _("Clear") + GetAccelerator(DO_CLEAR), _("Clear the selection"));
        menu->AppendSeparator();
        menu->Append(wxID_SELECTALL, _("Select All") + GetAccelerator(DO_SELALL), _("Select everything"));
        menuBar->Append(menu, _("&Edit"));
    }
    {   // view menu:
        wxMenu *menu = new wxMenu;
        menu->Append(ID::FullScreen, _("Full Screen") + GetAccelerator(DO_FULLSCREEN), _("Toggle full screen mode"));
        menu->Append(ID::FitPattern, _("Fit Pattern") + GetAccelerator(DO_FIT), _("Restore view so all of pattern is visible"));
        menu->AppendCheckItem(ID::Wireframe, _("Wireframe") + GetAccelerator(DO_WIREFRAME), _("Wireframe or surface view"));
        menu->AppendSeparator();
        menu->AppendCheckItem(ID::PatternsPane, _("&Patterns Pane") + GetAccelerator(DO_PATTERNS), _("View the patterns pane"));
        menu->AppendCheckItem(ID::RulePane, _("&Rule Pane") + GetAccelerator(DO_RULE), _("View the rule pane"));
        menu->AppendCheckItem(ID::HelpPane, _("&Help Pane") + GetAccelerator(DO_HELP), _("View the help pane"));
        menu->AppendSeparator();
        menu->Append(ID::RestoreDefaultPerspective, _("&Restore Default Layout") + GetAccelerator(DO_RESTORE), _("Put the windows back where they were"));
        menu->AppendSeparator();
        menu->Append(ID::ChangeActiveChemical, _("&Change Active Chemical...") + GetAccelerator(DO_CHEMICAL), _("Change which chemical is being visualized"));
        menuBar->Append(menu, _("&View"));
    }
    {   // action menu:
        wxMenu *menu = new wxMenu;
        menu->Append(ID::Step, _("&Step") + GetAccelerator(DO_STEP), _("Advance the simulation by a single timestep"));
        menu->Append(ID::RunStop, _("&Run") + GetAccelerator(DO_RUNSTOP), _("Start running the simulation"));
        menu->AppendSeparator();
        menu->Append(ID::Reset, _("Reset") + GetAccelerator(DO_RESET), _("Go back to the starting pattern"));
        menu->Append(ID::GenerateInitialPattern, _("Generate &Pattern") + GetAccelerator(DO_GENPATT), _("Run the Initial Pattern Generator"));
        menu->AppendSeparator();
        menu->Append(ID::SelectOpenCLDevice, _("Select OpenCL &Device...") + GetAccelerator(DO_DEVICE), _("Choose which OpenCL device to run on"));
        menu->Append(ID::OpenCLDiagnostics, _("Show Open&CL Diagnostics...") + GetAccelerator(DO_OPENCL), _("Show the available OpenCL devices and their attributes"));
        menuBar->Append(menu, _("&Action"));
    }
    {   // help menu:
        wxMenu *menu = new wxMenu;
        menu->Append(wxID_HELP,        _("Contents"));
        menu->Append(ID::HelpQuick,    _("Quick Start"));
        menu->Append(ID::HelpTips,     _("Hints and Tips"));
        menu->Append(ID::HelpKeyboard, _("Keyboard Shortcuts"));
        menu->Append(ID::HelpMouse,    _("Mouse Shortcuts"));
        menu->AppendSeparator();
        menu->Append(ID::HelpFile,     _("File Menu"));
        menu->Append(ID::HelpEdit,     _("Edit Menu"));
        menu->Append(ID::HelpView,     _("View Menu"));
        menu->Append(ID::HelpAction,   _("Action Menu"));
        menu->Append(ID::HelpHelp,     _("Help Menu"));
        menu->AppendSeparator();
        menu->Append(ID::HelpRefs,     _("References"));
        menu->Append(ID::HelpFormats,  _("File Formats"));
        menu->Append(ID::HelpProblems, _("Known Problems"));
        menu->Append(ID::HelpChanges,  _("Changes"));
        menu->Append(ID::HelpCredits,  _("Credits"));
        menu->AppendSeparator();
        menu->Append(wxID_ABOUT,       _("&About Ready") + GetAccelerator(DO_ABOUT));
        menuBar->Append(menu, _("&Help"));
    }
    SetMenuBar(menuBar);
}

#ifdef __WXMAC__
    // smaller toolbar bitmaps look much nicer on Mac
    #undef wxART_TOOLBAR
    #define wxART_TOOLBAR wxART_BUTTON
#endif

void MyFrame::InitializeToolbars()
{
    {   // file menu items
        wxAuiToolBar *tb = new wxAuiToolBar(this,ID::FileToolbar);
        tb->AddTool(wxID_NEW,wxEmptyString,wxArtProvider::GetBitmap(wxART_NEW,wxART_TOOLBAR),_("Create a new pattern"));
        tb->AddTool(wxID_OPEN,wxEmptyString,wxArtProvider::GetBitmap(wxART_FILE_OPEN,wxART_TOOLBAR),_("Choose a pattern file to open"));
        tb->AddTool(wxID_SAVE,wxEmptyString,wxArtProvider::GetBitmap(wxART_FILE_SAVE,wxART_TOOLBAR),_("Save the current pattern"));
        #ifdef __WXMAC__
            tb->SetToolBorderPadding(10);
        #endif
        this->aui_mgr.AddPane(tb,wxAuiPaneInfo().ToolbarPane().Top().Name(PaneName(ID::FileToolbar))
            .Position(0).Caption(_("File tools")));
    }
    {   // action menu items
        this->action_toolbar = new wxAuiToolBar(this,ID::ActionToolbar);
        this->action_toolbar->AddTool(ID::RunStop,wxEmptyString,wxArtProvider::GetBitmap(wxART_GO_FORWARD,wxART_TOOLBAR),
            _("Start running the simulation"));
        this->action_toolbar->AddTool(ID::Reset,wxEmptyString,wxArtProvider::GetBitmap(wxART_GOTO_FIRST,wxART_TOOLBAR),
            _("Go back to the starting pattern"));
        this->action_toolbar->AddTool(ID::GenerateInitialPattern,wxEmptyString,wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE,wxART_TOOLBAR),
            _("Run the Initial Pattern Generator"));
        #ifdef __WXMAC__
            this->action_toolbar->SetToolBorderPadding(10);
        #endif
        this->aui_mgr.AddPane(this->action_toolbar,wxAuiPaneInfo().ToolbarPane().Top()
            .Name(PaneName(ID::ActionToolbar)).Position(1).Caption(_("Action tools")));
    }
}

void MyFrame::InitializePatternsPane()
{
    this->patterns_panel = new PatternsPanel(this,wxID_ANY);
    this->aui_mgr.AddPane(this->patterns_panel,
                  wxAuiPaneInfo()
                  .Name(PaneName(ID::PatternsPane))
                  .Caption(_("Patterns Pane"))
                  .Left()
                  .BestSize(300,300)
                  .Layer(0) // layer 0 is the innermost ring around the central pane
                  );
}

void MyFrame::InitializeRulePane()
{
    this->rule_panel = new RulePanel(this,wxID_ANY);
    this->aui_mgr.AddPane(this->rule_panel,
                  wxAuiPaneInfo()
                  .Name(PaneName(ID::RulePane))
                  .Caption(_("Rule Pane"))
                  .Right()
                  .BestSize(400,400)
                  .Layer(1) // layer 1 is further towards the edge
                  .Hide()
                  );
}

void MyFrame::UpdateRulePane()
{
    this->rule_panel->Update(this->system);
}

void MyFrame::InitializeHelpPane()
{
    this->help_panel = new HelpPanel(this,wxID_ANY);
    this->aui_mgr.AddPane(this->help_panel,
                  wxAuiPaneInfo()
                  .Name(PaneName(ID::HelpPane))
                  .Caption(_("Help Pane"))
                  .Right()
                  .BestSize(400,400)
                  .Hide()
                  );
}

// -----------------------------------------------------------------------------

#if wxUSE_DRAG_AND_DROP

// derive a simple class for handling dropped files
class DnDFile : public wxFileDropTarget
{
public:
    DnDFile() {}
    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames);
};

bool DnDFile::OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames)
{
    MyFrame* frameptr = wxGetApp().currframe;

    // bring app to front
    #ifdef __WXMAC__
        ProcessSerialNumber process;
        if ( GetCurrentProcess(&process) == noErr )
            SetFrontProcess(&process);
    #endif
    #ifdef __WXMSW__
        SetForegroundWindow( (HWND)frameptr->GetHandle() );
    #endif
    frameptr->Raise();
   
    size_t numfiles = filenames.GetCount();
    for ( size_t n = 0; n < numfiles; n++ ) {
        frameptr->OpenFile(filenames[n]);
    }
    return true;
}

#endif // wxUSE_DRAG_AND_DROP

// -----------------------------------------------------------------------------

void MyFrame::InitializeRenderPane()
{
    // for now the VTK window goes in the center pane (always visible) - we got problems when had in a floating pane
    vtkObject::GlobalWarningDisplayOff(); // (can turn on for debugging)
    this->pVTKWindow = new wxVTKRenderWindowInteractor(this,wxID_ANY);
    this->aui_mgr.AddPane(this->pVTKWindow, wxAuiPaneInfo()
                  .Name(PaneName(ID::CanvasPane))
                  .CenterPane()
                  .BestSize(400,400)
                  );

    #if wxUSE_DRAG_AND_DROP
        // let users drag-and-drop pattern files onto the render pane
        this->pVTKWindow->SetDropTarget(new DnDFile());
    #endif

    // install event handler to detect keyboard shortcuts when render window has focus
    this->pVTKWindow->Connect(wxEVT_CHAR, wxKeyEventHandler(MyFrame::OnChar), NULL, this);
}

void MyFrame::LoadSettings()
{
    // use global info set by GetPrefs()
    this->SetPosition(wxPoint(mainx,mainy));
    this->SetSize(mainwd,mainht);
    if (auilayout.length() > 0) this->aui_mgr.LoadPerspective(auilayout);
}

void MyFrame::SaveSettings()
{
    if (fullscreen) {
        // use auilayout saved earlier in OnFullScreen
    } else {
        auilayout = this->aui_mgr.SavePerspective();
    }
    SavePrefs();
}

MyFrame::~MyFrame()
{
    this->SaveSettings(); // save the current settings so it starts up the same next time
    this->aui_mgr.UnInit();
    this->pVTKWindow->Delete();
    delete this->system;
    this->starting_pattern->Delete();
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    if(UserWantsToCancelWhenAskedIfWantsToSave()) return;
    Close(true);
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxAboutDialogInfo info;
    wxString title;
    title << _T("Ready ") << _T(STR(READY_VERSION));
    info.SetName(title);
    info.SetDescription(_("A program for exploring reaction-diffusion systems.\n\nReady is free software, distributed under the GPLv3 license."));
    info.SetCopyright(_T("(C) 2011, 2012 The Ready Bunch"));
    info.AddDeveloper(_T("Tim Hutton"));
    info.AddDeveloper(_T("Robert Munafo"));
    info.AddDeveloper(_T("Andrew Trevorrow"));
    info.AddDeveloper(_T("Tom Rokicki"));
    wxAboutBox(info);
}

void MyFrame::OnCut(wxCommandEvent& event)
{
    // TODO: action depends on which pane has focus
    if (this->help_panel->HtmlHasFocus()) return;
    event.Skip();
}

void MyFrame::OnCopy(wxCommandEvent& event)
{
    // TODO: action depends on which pane has focus
    if (this->help_panel->HtmlHasFocus()) {
        this->help_panel->CopySelection();
        return;
    }
    event.Skip();
}

void MyFrame::OnPaste(wxCommandEvent& event)
{
    // TODO: action depends on which pane has focus
    if (this->help_panel->HtmlHasFocus()) return;
    event.Skip();
}

void MyFrame::OnUpdatePaste(wxUpdateUIEvent& event)
{
    event.Enable(ClipboardHasText());
}

void MyFrame::OnClear(wxCommandEvent& event)
{
    // TODO: action depends on which pane has focus
    if (this->help_panel->HtmlHasFocus()) return;
    event.Skip();
}

void MyFrame::OnSelectAll(wxCommandEvent& event)
{
    // TODO: action depends on which pane has focus, if any
    if (this->help_panel->HtmlHasFocus()) {
        this->help_panel->SelectAllText();
        return;
    }
    event.Skip();
}

void MyFrame::OnFullScreen(wxCommandEvent& event)
{
    static bool restorestatus;  // restore the status bar?
    
    wxStatusBar* statusbar = GetStatusBar();
    
    if (!fullscreen) {
        // save current location and size for use in SavePrefs
        wxRect r = GetRect();
        mainx = r.x;
        mainy = r.y;
        mainwd = r.width;
        mainht = r.height;
        // also save current perspective
        auilayout = this->aui_mgr.SavePerspective();
    } else {
        // restore status bar before calling ShowFullScreen (so we see status text in Mac app)
        if (restorestatus) statusbar->Show();
    }

    fullscreen = !fullscreen;
    ShowFullScreen(fullscreen, wxFULLSCREEN_NOMENUBAR | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);

    if (fullscreen) {
        // hide the status bar
        restorestatus = statusbar && statusbar->IsShown();
        if (restorestatus) statusbar->Hide();

        // hide all currently shown panes
        wxAuiPaneInfo &pattpane = this->aui_mgr.GetPane(PaneName(ID::PatternsPane));
        wxAuiPaneInfo &rulepane = this->aui_mgr.GetPane(PaneName(ID::RulePane));
        wxAuiPaneInfo &helppane = this->aui_mgr.GetPane(PaneName(ID::HelpPane));
        wxAuiPaneInfo &filepane = this->aui_mgr.GetPane(PaneName(ID::FileToolbar));
        wxAuiPaneInfo &actionpane = this->aui_mgr.GetPane(PaneName(ID::ActionToolbar));
        
        if (pattpane.IsOk() && pattpane.IsShown()) pattpane.Show(false);
        if (rulepane.IsOk() && rulepane.IsShown()) rulepane.Show(false);
        if (helppane.IsOk() && helppane.IsShown()) helppane.Show(false);
        if (filepane.IsOk() && filepane.IsShown()) filepane.Show(false);
        if (actionpane.IsOk() && actionpane.IsShown()) actionpane.Show(false);
        
        // ensure the render window sees keyboard shortcuts
        this->pVTKWindow->SetFocus();

    } else {
        // restore saved perspective
        this->aui_mgr.LoadPerspective(auilayout);
    }
    
    this->aui_mgr.Update();
}

void MyFrame::OnFitPattern(wxCommandEvent& event)
{
    this->pVTKWindow->DoCharEvent('r');
}

void MyFrame::OnWireframe(wxCommandEvent& event)
{
    this->pVTKWindow->DoCharEvent(this->is_wireframe ? 's' : 'w');
    this->is_wireframe = !this->is_wireframe;
}

void MyFrame::OnUpdateWireframe(wxUpdateUIEvent& event)
{
    event.Check(this->is_wireframe);
}

void MyFrame::OnToggleViewPane(wxCommandEvent& event)
{
    wxAuiPaneInfo &pane = this->aui_mgr.GetPane(PaneName(event.GetId()));
    if(!pane.IsOk()) return;
    pane.Show(!pane.IsShown());
    this->aui_mgr.Update();
}

void MyFrame::OnUpdateViewPane(wxUpdateUIEvent& event)
{
    wxAuiPaneInfo &pane = this->aui_mgr.GetPane(PaneName(event.GetId()));
    if(!pane.IsOk()) return;
    event.Check(pane.IsShown());
    // AKT: following call isn't necessary and can cause unwanted flashing
    // in help pane (due to HtmlView::OnSize being called)
    // this->aui_mgr.Update();
}

void MyFrame::OnOpenCLDiagnostics(wxCommandEvent& event)
{
    // TODO: merge this with SelectOpenCLDevice?
    wxBusyCursor busy;
    wxMessageBox(wxString(OpenCL_RD::GetOpenCLDiagnostics().c_str(),wxConvUTF8));
}

void MyFrame::OnSize(wxSizeEvent& event)
{
#ifdef __WXMSW__
    if(this->pVTKWindow) {
        // save current location and size for use in SavePrefs if app
        // is closed when window is minimized
        wxRect r = GetRect();
        mainx = r.x;
        mainy = r.y;
        mainwd = r.width;
        mainht = r.height;
    }
#endif

    // trigger a redraw
    if(this->pVTKWindow) this->pVTKWindow->Refresh(false);
    
    // need this to move and resize status bar in Mac app
    event.Skip();
}

void MyFrame::OnScreenshot(wxCommandEvent& event)
{
    // find an unused filename
    const wxString default_filename_root = _("Ready_screenshot_");
    const wxString default_filename_ext = _T("png");
    int unused_value = 0;
    wxString filename;
    wxString extension,folder;
    folder = screenshotdir;
    do {
        filename = default_filename_root;
        filename << wxString::Format(_("%04d."),unused_value) << default_filename_ext;
        unused_value++;
    } while(::wxFileExists(folder+_T("/")+filename));

    // ask the user for confirmation
    bool accepted = true;
    do {
        filename = wxFileSelector(_("Specify the screenshot filename"),folder,filename,default_filename_ext,
            _("PNG files (*.png)|*.png|JPG files (*.jpg)|*.jpg"),
            wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(filename.empty()) return; // user cancelled
        // validate
        wxFileName::SplitPath(filename,&folder,NULL,&extension);
        if(extension!=_T("png") && extension!=_T("jpg"))
        {
            wxMessageBox(_("Unsupported format"));
            accepted = false;
        }
    } while(!accepted);

    screenshotdir = folder;

    vtkSmartPointer<vtkWindowToImageFilter> screenshot = vtkSmartPointer<vtkWindowToImageFilter>::New();
    screenshot->SetInput(this->pVTKWindow->GetRenderWindow());

    vtkSmartPointer<vtkImageWriter> writer;
    if(extension==_T("png")) writer = vtkSmartPointer<vtkPNGWriter>::New();
    else if(extension==_T("jpg")) writer = vtkSmartPointer<vtkJPEGWriter>::New();
    writer->SetFileName(filename.mb_str());
    writer->SetInputConnection(screenshot->GetOutputPort());
    writer->Write();
}

void MyFrame::SetCurrentRDSystem(BaseRD* sys)
{
    delete this->system;
    this->system = sys;
    this->iActiveChemical = min(this->iActiveChemical,this->system->GetNumberOfChemicals()-1);
    InitializeVTKPipeline(this->pVTKWindow,this->system,this->iActiveChemical);
    this->is_running = false;
    this->UpdateWindows();
}

void MyFrame::UpdateWindowTitle()
{
    wxString name = this->system->GetFilename();
    if (name.IsEmpty()) {
        // this should probably never happen
        name = _("unknown");
    } else {
        // just show file's name, not full path
        // AKT TODO!!! perhaps we should make this optional (via flag in Prefs > File)???
        name = name.AfterLast(wxFILE_SEP_PATH);
    }
    
    if (this->system->IsModified()) {
        // prepend asterisk to indicate the current system has been modified
        // (this is consistent with Golly and other Win/Linux apps)
        name = _T("*") + name;
    }
    
    #ifdef __WXMAC__
        // Mac apps don't show app name in window title
        this->SetTitle(name);
    #else
        // Win/Linux apps usually append the app name to the file name
        this->SetTitle(name + _T(" - Ready"));
    #endif
}

void MyFrame::UpdateWindows()
{
    this->SetStatusBarText();
    this->UpdateRulePane();
    this->UpdateWindowTitle();
    this->UpdateToolbars();
    this->Refresh(false);
}

void MyFrame::OnStep(wxCommandEvent& event)
{
    if(this->system->GetTimestepsTaken()==0)
        this->SaveStartingPattern();

    try {
        this->system->Update(1);
    }
    catch(const exception& e)
    {
        wxMessageBox(_("Fatal error: ")+wxString(e.what(),wxConvUTF8));
        this->Destroy();
    }
    catch(...)
    {
        wxMessageBox(_("Unknown fatal error"));
        this->Destroy();
    }
    this->SetStatusBarText();
    Refresh(false);
}

void MyFrame::OnUpdateStep(wxUpdateUIEvent& event)
{
    event.Enable(!this->is_running);
}

void MyFrame::OnRunStop(wxCommandEvent& event)
{
    if(this->is_running) {
        this->is_running = false;
        this->SetStatusBarText();
    } else {
        this->is_running = true;
    }
    this->UpdateToolbars();
    Refresh(false);
}

void MyFrame::OnUpdateRunStop(wxUpdateUIEvent& event)
{
    wxMenuBar* mbar = GetMenuBar();
    if(mbar) {
        if(this->is_running) {
            mbar->SetLabel(ID::RunStop, _("Stop") + GetAccelerator(DO_RUNSTOP));
            mbar->SetHelpString(ID::RunStop,_("Stop running the simulation"));
        } else {
            mbar->SetLabel(ID::RunStop, _("Run") + GetAccelerator(DO_RUNSTOP));
            mbar->SetHelpString(ID::RunStop,_("Start running the simulation"));
        }
    }
}

void MyFrame::UpdateToolbars()
{
    this->action_toolbar->FindTool(ID::RunStop)->SetBitmap( 
        this->is_running ? wxArtProvider::GetBitmap(wxART_CROSS_MARK,wxART_TOOLBAR)
                         : wxArtProvider::GetBitmap(wxART_GO_FORWARD,wxART_TOOLBAR) );
    
    this->action_toolbar->FindTool(ID::RunStop)->SetShortHelp( 
        this->is_running ? _("Stop running the simulation")
                         : _("Start running the simulation") );
}

void MyFrame::OnReset(wxCommandEvent& event)
{
    if(this->system->GetTimestepsTaken() > 0) 
    {
        // restore pattern and other info saved by SaveStartingPattern() which
        // was called in OnStep/OnRunStop when GetTimestepsTaken() was 0
        this->RestoreStartingPattern();
        this->is_running = false;
        this->UpdateWindows();
    }
}

void MyFrame::SaveStartingPattern()
{
    this->starting_pattern->DeepCopy(this->system->GetImage());
}

void MyFrame::RestoreStartingPattern()
{
    this->system->CopyFromImage(this->starting_pattern);
    this->system->SetTimestepsTaken(0);
}

void MyFrame::OnUpdateReset(wxUpdateUIEvent& event)
{
    event.Enable(this->system->GetTimestepsTaken() > 0);
}

void MyFrame::OnIdle(wxIdleEvent& event)
{
    this->patterns_panel->DoIdleChecks();
    
    // we drive our game loop by onIdle events
    if(this->is_running)
    {
        if(this->system->GetTimestepsTaken()==0)
            this->SaveStartingPattern();

        int n_cells = this->system->GetX() * this->system->GetY() * this->system->GetZ();
   
        double time_before = get_time_in_seconds();
   
        try 
        {
            this->system->Update(this->timesteps_per_render); // TODO: user controls speed
        }
        catch(const exception& e)
        {
            wxMessageBox(_("Fatal error: ")+wxString(e.what(),wxConvUTF8));
            this->Destroy();
        }
        catch(...)
        {
            wxMessageBox(_("Unknown fatal error"));
            this->Destroy();
        }
   
        double time_after = get_time_in_seconds();
        this->frames_per_second = this->timesteps_per_render / (time_after - time_before);
        this->million_cell_generations_per_second = this->frames_per_second * n_cells / 1e6;
   
        this->pVTKWindow->Refresh(false);
        this->SetStatusBarText();
   
        wxMilliSleep(30);
   
        event.RequestMore(); // trigger another onIdle event
    }
    
    event.Skip();
}

void MyFrame::SetStatusBarText()
{
    wxString txt;
    if(this->is_running) txt << _("Running.");
    else txt << _("Stopped.");
    txt << _(" Timesteps: ") << this->system->GetTimestepsTaken();
    txt << _T("   (") << wxString::Format(_T("%.0f"),this->frames_per_second) 
        << _(" computed frames per second, ")
        << wxString::Format(_T("%.0f"),this->million_cell_generations_per_second) 
        << _T(" mcgs)");
    SetStatusText(txt);
}

void MyFrame::OnRestoreDefaultPerspective(wxCommandEvent& event)
{
    this->aui_mgr.LoadPerspective(this->default_perspective);
}

void MyFrame::OnGenerateInitialPattern(wxCommandEvent& event)
{
    if(UserWantsToCancelWhenAskedIfWantsToSave()) return;
    
    this->system->GenerateInitialPattern();

    this->is_running = false;
    this->system->SetFilename("untitled");
    this->system->SetModified(false);
    this->UpdateWindows();
}

void MyFrame::OnSelectOpenCLDevice(wxCommandEvent& event)
{
    // TODO: merge this with GetOpenCL diagnostics?
    wxArrayString choices;
    int iOldSelection;
    int np;
    try 
    {
        np = OpenCL_RD::GetNumberOfPlatforms();
    }
    catch(const exception& e)
    {
        wxMessageBox(_("OpenCL not available: ")+
            wxString(e.what(),wxConvUTF8));
        return;
    }
    catch(...)
    {
        wxMessageBox(_("OpenCL not available"));
        return;
    }
    for(int ip=0;ip<np;ip++)
    {
        int nd = OpenCL_RD::GetNumberOfDevices(ip);
        for(int id=0;id<nd;id++)
        {
            if(ip==opencl_platform && id==opencl_device)
                iOldSelection = (int)choices.size();
            wxString s(OpenCL_RD::GetPlatformDescription(ip).c_str(),wxConvUTF8);
            s << _T(" : ") << wxString(OpenCL_RD::GetDeviceDescription(ip,id).c_str(),wxConvUTF8);
            choices.Add(s);
        }
    }
    wxSingleChoiceDialog dlg(this,_("Select the OpenCL device to use:"),_("Select OpenCL device"),
        choices);
    dlg.SetSelection(iOldSelection);
    if(dlg.ShowModal()!=wxID_OK) return;
    int iNewSelection = dlg.GetSelection();
    if(iNewSelection != iOldSelection)
        wxMessageBox(_("The selected device will be used the next time an OpenCL pattern is loaded."));
    int dc = 0;
    for(int ip=0;ip<np;ip++)
    {
        int nd = OpenCL_RD::GetNumberOfDevices(ip);
        if(iNewSelection < nd)
        {
            opencl_platform = ip;
            opencl_device = iNewSelection;
            break;
        }
        iNewSelection -= nd;
    }
    // TODO: hot-change the current RD system
}

void MyFrame::OnHelp(wxCommandEvent& event)
{
    int id = event.GetId();
    switch (id)
    {
        case wxID_HELP:         this->help_panel->ShowHelp(_("Help/index.html")); break;
        case ID::HelpQuick:     this->help_panel->ShowHelp(_("Help/quickstart.html")); break;
        case ID::HelpTips:      this->help_panel->ShowHelp(_("Help/tips.html")); break;
        case ID::HelpKeyboard:  this->help_panel->ShowHelp(SHOW_KEYBOARD_SHORTCUTS); break;
        case ID::HelpMouse:     this->help_panel->ShowHelp(_("Help/mouse.html")); break;
        case ID::HelpFile:      this->help_panel->ShowHelp(_("Help/file.html")); break;
        case ID::HelpEdit:      this->help_panel->ShowHelp(_("Help/edit.html")); break;
        case ID::HelpView:      this->help_panel->ShowHelp(_("Help/view.html")); break;
        case ID::HelpAction:    this->help_panel->ShowHelp(_("Help/action.html")); break;
        case ID::HelpHelp:      this->help_panel->ShowHelp(_("Help/help.html")); break;
        case ID::HelpRefs:      this->help_panel->ShowHelp(_("Help/refs.html")); break;
        case ID::HelpFormats:   this->help_panel->ShowHelp(_("Help/formats.html")); break;
        case ID::HelpProblems:  this->help_panel->ShowHelp(_("Help/problems.html")); break;
        case ID::HelpChanges:   this->help_panel->ShowHelp(_("Help/changes.html")); break;
        case ID::HelpCredits:   this->help_panel->ShowHelp(_("Help/credits.html")); break;
        default:
            wxMessageBox(_("Bug: Unexpected ID in OnHelp!"));
            return;
    }
    
    wxAuiPaneInfo &pane = this->aui_mgr.GetPane(PaneName(ID::HelpPane));
    if(pane.IsOk() && !pane.IsShown()) {
        pane.Show();
        this->aui_mgr.Update();
    }
}

wxString MyFrame::SavePatternDialog()
{
    wxString filename = wxEmptyString;
    wxString currname = this->system->GetFilename();
    currname = currname.AfterLast(wxFILE_SEP_PATH);
    
    wxFileDialog savedlg(this, _("Specify the pattern filename"), opensavedir, currname,
                         _("VTK image files (*.vti)|*.vti"),
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    #ifdef __WXGTK__
        // opensavedir is ignored above (bug in wxGTK 2.8.0???)
        savedlg.SetDirectory(opensavedir);
    #endif
    if(savedlg.ShowModal() == wxID_OK) {
        wxFileName fullpath( savedlg.GetPath() );
        opensavedir = fullpath.GetPath();
        filename = savedlg.GetPath();
    }
    
    return filename;
}

void MyFrame::OnSavePattern(wxCommandEvent& event)
{
    wxString filename = SavePatternDialog();
    if(!filename.empty()) SaveFile(filename);
}

void MyFrame::SaveFile(const wxString& path)
{
    wxBusyCursor busy;

    vtkSmartPointer<RD_XMLWriter> iw = vtkSmartPointer<RD_XMLWriter>::New();
    iw->SetSystem(this->system);
    iw->SetFileName(path.mb_str());
    iw->Write();

    AddRecentPattern(path);
    this->system->SetFilename(string(path.mb_str()));
    this->system->SetModified(false);
    this->UpdateWindowTitle();
}

void MyFrame::OnNewPattern(wxCommandEvent& event)
{
    if(this->system == NULL) {
        // initial call from MyFrame::MyFrame
        GrayScott *s = new GrayScott();
        s->Allocate(30,25,20,2);
        s->SetModified(false);
        s->SetFilename("untitled");
        s->GenerateInitialPattern();
        this->SetCurrentRDSystem(s);
        return;
    }
    
    if(UserWantsToCancelWhenAskedIfWantsToSave()) return;
    
    this->system->BlankImage();

    this->is_running = false;
    this->system->SetFilename("untitled");
    this->system->SetModified(false);
    this->UpdateWindows();
}

void MyFrame::OnOpenPattern(wxCommandEvent& event)
{
    if(UserWantsToCancelWhenAskedIfWantsToSave()) return;
        
    wxFileDialog opendlg(this, _("Choose a pattern file"), opensavedir, wxEmptyString,
                         _("VTK image files (*.vti)|*.vti"),
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    #ifdef __WXGTK__
        // opensavedir is ignored above (bug in wxGTK 2.8.x???)
        opendlg.SetDirectory(opensavedir);
    #endif
    if(opendlg.ShowModal() == wxID_OK) {
        wxFileName fullpath( opendlg.GetPath() );
        opensavedir = fullpath.GetPath();
        OpenFile( opendlg.GetPath() );
    }
}

void MyFrame::OpenFile(const wxString& path, bool remember)
{
    if (IsHTMLFile(path)) {
        // show HTML file in help pane
        this->help_panel->ShowHelp(path);
    
        wxAuiPaneInfo &pane = this->aui_mgr.GetPane(PaneName(ID::HelpPane));
        if(pane.IsOk() && !pane.IsShown()) {
            pane.Show();
            this->aui_mgr.Update();
        }
        
        return;
    }
    
    if (IsTextFile(path)) {
        // open text file in user's preferred text editor
        EditFile(path);
        return;
    }

    if (remember) AddRecentPattern(path);
    
    // load pattern file
    bool warn_to_update = false;
    BaseRD *target_system = NULL;
    try
    {
        wxBusyCursor busy;

        vtkSmartPointer<RD_XMLReader> iw = vtkSmartPointer<RD_XMLReader>::New();
        iw->SetFileName(path.mb_str());
        iw->Update();

        string type = iw->GetType();
        if(type=="inbuilt")
        {
            string name = iw->GetName();
            if(name=="Gray-Scott")
                target_system = new GrayScott();
            else 
                throw runtime_error("Unsupported inbuilt implementation: "+name);
        }
        else if(type=="formula")
        {
            // to load type="formula" pattern files the implementation must support editable kernels, which for now means OpenCL_nDim
            // TODO: detect if opencl is available, abort if not
            OpenCL_nDim *s = new OpenCL_nDim();
            s->SetPlatform(opencl_platform);
            s->SetDevice(opencl_device);
            target_system = s;
        }
        iw->SetSystemFromXML(target_system,warn_to_update);

        int dim[3];
        iw->GetOutput()->GetDimensions(dim);
        int nc = iw->GetOutput()->GetNumberOfScalarComponents();
        target_system->Allocate(dim[0],dim[1],dim[2],nc);
        if(iw->ShouldGenerateInitialPatternWhenLoading())
            target_system->GenerateInitialPattern();
        else
            target_system->CopyFromImage(iw->GetOutput());
        target_system->SetFilename(string(path.mb_str())); // TODO: display filetitle only (user option?)
        target_system->SetModified(false);
        this->SetCurrentRDSystem(target_system);
    }
    catch(const exception& e)
    {
        if(warn_to_update)
            wxMessageBox(_("This file is from a more recent version of Ready. You should download a newer version.\n\nError: ")
                +wxString(e.what(),wxConvUTF8),_("Error reading file"),wxOK | wxICON_ERROR);
        else
            wxMessageBox(_("Failed to open file: ")+wxString(e.what(),wxConvUTF8),_("Error reading file"),wxOK | wxICON_ERROR);
        delete target_system;
        return;
    }
    catch(...)
    {
        if(warn_to_update)
            wxMessageBox(_("This file is from a more recent version of Ready. You should download a newer version."),_("Error reading file"),wxOK | wxICON_ERROR);
        else
            wxMessageBox(_("Failed to open file"),_("Error reading file"),wxOK | wxICON_ERROR);
        delete target_system;
        return;
    }
    if(warn_to_update)
    {
        wxMessageBox("This file is from a more recent version of Ready. For best results you should download a newer version.");
        // TODO: allow user to stop this message from appearing every time
    }
}

void MyFrame::OnOpenRecent(wxCommandEvent& event)
{
    int id = event.GetId();
    if (id == ID::ClearMissingPatterns) {
        ClearMissingPatterns();
    } else if (id == ID::ClearAllPatterns) {
        ClearAllPatterns();
    } else if ( id > ID::OpenRecent && id <= ID::OpenRecent + numpatterns ) {
        OpenRecentPattern(id);
    } else {
        event.Skip();
    }
}

void MyFrame::AddRecentPattern(const wxString& inpath)
{
    if (inpath.IsEmpty()) return;
    wxString path = inpath;
    if (path.StartsWith(readydir)) {
        // remove readydir from start of path
        path.erase(0, readydir.length());
    }

    // duplicate any ampersands so they appear in menu
    path.Replace(wxT("&"), wxT("&&"));

    // put given path at start of patternSubMenu
    #ifdef __WXGTK__
        // avoid wxGTK bug in FindItem if path contains underscores
        int id = wxNOT_FOUND;
        for (int i = 0; i < numpatterns; i++) {
            wxMenuItem* item = patternSubMenu->FindItemByPosition(i);
            wxString temp = item->GetText();
            temp.Replace(wxT("__"), wxT("_"));
            temp.Replace(wxT("&"), wxT("&&"));
            if (temp == path) {
                id = ID::OpenRecent + 1 + i;
                break;
            }
        }
    #else
        int id = patternSubMenu->FindItem(path);
    #endif
    if ( id == wxNOT_FOUND ) {
        if ( numpatterns < maxpatterns ) {
            // add new path
            numpatterns++;
            id = ID::OpenRecent + numpatterns;
            patternSubMenu->Insert(numpatterns - 1, id, path);
        } else {
            // replace last item with new path
            wxMenuItem* item = patternSubMenu->FindItemByPosition(maxpatterns - 1);
            item->SetText(path);
            id = ID::OpenRecent + maxpatterns;
        }
    }
    
    // path exists in patternSubMenu
    if ( id > ID::OpenRecent + 1 ) {
        // move path to start of menu
        wxMenuItem* item;
        while ( id > ID::OpenRecent + 1 ) {
            wxMenuItem* previtem = patternSubMenu->FindItem(id - 1);
            wxString prevpath = previtem->GetText();
            #ifdef __WXGTK__
                // remove duplicate underscores
                prevpath.Replace(wxT("__"), wxT("_"));
                prevpath.Replace(wxT("&"), wxT("&&"));
            #endif
            item = patternSubMenu->FindItem(id);
            item->SetText(prevpath);
            id--;
        }
        item = patternSubMenu->FindItem(id);
        item->SetText(path);
    }
    
    wxMenuBar* mbar = GetMenuBar();
    if (mbar) mbar->Enable(ID::OpenRecent, numpatterns > 0);
}

void MyFrame::OpenRecentPattern(int id)
{
    wxMenuItem* item = patternSubMenu->FindItem(id);
    if (item) {
        wxString path = item->GetText();
        #ifdef __WXGTK__
            // remove duplicate underscores
            path.Replace(wxT("__"), wxT("_"));
        #endif
        // remove duplicate ampersands
        path.Replace(wxT("&&"), wxT("&"));

        // if path isn't absolute then prepend Ready directory
        wxFileName fname(path);
        if (!fname.IsAbsolute()) path = readydir + path;

        OpenFile(path);
    }
}

void MyFrame::ClearMissingPatterns()
{
    int pos = 0;
    while (pos < numpatterns) {
        wxMenuItem* item = patternSubMenu->FindItemByPosition(pos);
        wxString path = item->GetText();
        #ifdef __WXGTK__
            // remove duplicate underscores
            path.Replace(wxT("__"), wxT("_"));
        #endif
        // remove duplicate ampersands
        path.Replace(wxT("&&"), wxT("&"));

        // if path isn't absolute then prepend Ready directory
        wxFileName fname(path);
        if (!fname.IsAbsolute()) path = readydir + path;

        if (wxFileExists(path)) {
            // keep this item
            pos++;
        } else {
            // remove this item by shifting up later items
            int nextpos = pos + 1;
            while (nextpos < numpatterns) {
                wxMenuItem* nextitem = patternSubMenu->FindItemByPosition(nextpos);
                #ifdef __WXGTK__
                    // avoid wxGTK bug if item contains underscore
                    wxString temp = nextitem->GetText();
                    temp.Replace(wxT("__"), wxT("_"));
                    temp.Replace(wxT("&"), wxT("&&"));
                    item->SetText( temp );
                #else
                    item->SetText( nextitem->GetText() );
                #endif
                item = nextitem;
                nextpos++;
            }
            // delete last item
            patternSubMenu->Delete(item);
            numpatterns--;
        }
    }
    wxMenuBar* mbar = GetMenuBar();
    if (mbar) mbar->Enable(ID::OpenRecent, numpatterns > 0);
}

void MyFrame::ClearAllPatterns()
{
    while (numpatterns > 0) {
        patternSubMenu->Delete( patternSubMenu->FindItemByPosition(0) );
        numpatterns--;
    }
    wxMenuBar* mbar = GetMenuBar();
    if (mbar) mbar->Enable(ID::OpenRecent, false);
}

void MyFrame::EditFile(const wxString& path)
{
    // prompt user if text editor hasn't been set yet
    if (texteditor.IsEmpty()) {
        ChooseTextEditor(this, texteditor);
        if (texteditor.IsEmpty()) return;
    }
    
    // open given file in user's preferred text editor
    wxString cmd;
    #ifdef __WXMAC__
        cmd = wxString::Format(wxT("open -a \"%s\" \"%s\""), texteditor.c_str(), path.c_str());
    #else
        // Windows or Unix
        cmd = wxString::Format(wxT("\"%s\" \"%s\""), texteditor.c_str(), path.c_str());
    #endif
    wxExecute(cmd, wxEXEC_ASYNC);
}

void MyFrame::OnChangeActiveChemical(wxCommandEvent& event)
{
    wxArrayString choices;
    for(int i=0;i<this->system->GetNumberOfChemicals();i++)
        choices.Add(wxString::Format(_T("%c"),'a'+i));
    wxSingleChoiceDialog dlg(this,_("Select the chemical to render:"),_("Select active chemical"),
        choices);
    dlg.SetSelection(this->iActiveChemical);
    if(dlg.ShowModal()!=wxID_OK) return;
    this->iActiveChemical = dlg.GetSelection();
    InitializeVTKPipeline(this->pVTKWindow,this->system,this->iActiveChemical);
    this->UpdateWindows();
    // TODO: might have some visualization based on more than one chemical
}

void MyFrame::SetRuleName(string s)
{
    this->system->SetRuleName(s);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetRuleDescription(string s)
{
    this->system->SetRuleDescription(s);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetPatternDescription(string s)
{
    this->system->SetPatternDescription(s);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetParameter(int iParam,float val)
{
    this->system->SetParameterValue(iParam,val);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetParameterName(int iParam,std::string s)
{
    this->system->SetParameterName(iParam,s);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetFormula(std::string s)
{
    this->system->SetFormula(s);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

void MyFrame::SetTimestep(float ts)
{
    this->system->SetTimestep(ts);
    this->UpdateWindowTitle();
    // TODO: update anything else that needs to know
}

bool MyFrame::UserWantsToCancelWhenAskedIfWantsToSave()
{
    if(!this->system->IsModified()) return false;
    
    int ret = SaveChanges(_("Save the current system?"),_("If you don't save, your changes will be lost."));
    if(ret==wxCANCEL) return true;
    if(ret==wxNO) return false;

    // ret == wxYES
    wxString filename = SavePatternDialog();
    if(filename.empty()) return true; // user cancelled

    SaveFile(filename);
    return false;
}

void MyFrame::OnClose(wxCloseEvent& event)
{
    if(event.CanVeto() && this->UserWantsToCancelWhenAskedIfWantsToSave()) return;
    event.Skip();
}

void MyFrame::UpdateMenuAccelerators()
{
    // keyboard shortcuts have changed, so update all menu item accelerators
    wxMenuBar* mbar = GetMenuBar();
    if (mbar) {
        // wxMac bug: these app menu items aren't updated (but user isn't likely
        // to change them so don't bother trying to fix the bug)
        SetAccelerator(mbar, wxID_ABOUT,                    DO_ABOUT);
        SetAccelerator(mbar, wxID_PREFERENCES,              DO_PREFS);
        SetAccelerator(mbar, wxID_EXIT,                     DO_QUIT);
        
        SetAccelerator(mbar, wxID_NEW,                      DO_NEWPATT);
        SetAccelerator(mbar, wxID_OPEN,                     DO_OPENPATT);
        SetAccelerator(mbar, wxID_SAVE,                     DO_SAVE);
        SetAccelerator(mbar, ID::Screenshot,                DO_SCREENSHOT);
        
        SetAccelerator(mbar, wxID_CUT,                      DO_CUT);
        SetAccelerator(mbar, wxID_COPY,                     DO_COPY);
        SetAccelerator(mbar, wxID_PASTE,                    DO_PASTE);
        SetAccelerator(mbar, wxID_CLEAR,                    DO_CLEAR);
        SetAccelerator(mbar, wxID_SELECTALL,                DO_SELALL);
        
        SetAccelerator(mbar, ID::FullScreen,                DO_FULLSCREEN);
        SetAccelerator(mbar, ID::FitPattern,                DO_FIT);
        SetAccelerator(mbar, ID::Wireframe,                 DO_WIREFRAME);
        SetAccelerator(mbar, ID::PatternsPane,              DO_PATTERNS);
        SetAccelerator(mbar, ID::RulePane,                  DO_RULE);
        SetAccelerator(mbar, ID::HelpPane,                  DO_HELP);
        SetAccelerator(mbar, ID::RestoreDefaultPerspective, DO_RESTORE);
        SetAccelerator(mbar, ID::ChangeActiveChemical,      DO_CHEMICAL);
        
        SetAccelerator(mbar, ID::Step,                      DO_STEP);
        SetAccelerator(mbar, ID::RunStop,                   DO_RUNSTOP);
        SetAccelerator(mbar, ID::Reset,                     DO_RESET);
        SetAccelerator(mbar, ID::GenerateInitialPattern,    DO_GENPATT);
        SetAccelerator(mbar, ID::SelectOpenCLDevice,        DO_DEVICE);
        SetAccelerator(mbar, ID::OpenCLDiagnostics,         DO_OPENCL);
    }
}

void MyFrame::ShowPrefsDialog(const wxString& page)
{
    if (ChangePrefs(page)) {
        // user hit OK button so might as well save prefs now
        SaveSettings();
    }
    // safer to update everything even if user hit Cancel
    this->UpdateWindows();
}

void MyFrame::OnPreferences(wxCommandEvent& event)
{
    ShowPrefsDialog();
}

void MyFrame::ProcessKey(int key, int modifiers)
{
    int cmdid = 0;
    action_info action = FindAction(key, modifiers);
    
    switch (action.id)
    {
        case DO_NOTHING:    // any unassigned key (including escape) turns off full screen mode
                            if (fullscreen) cmdid = ID::FullScreen; break;
        
        case DO_OPENFILE:   OpenFile(action.file);
                            return;
        
        // File menu
        case DO_NEWPATT:    cmdid = wxID_NEW; break;
        case DO_OPENPATT:   cmdid = wxID_OPEN; break;
        case DO_SAVE:       cmdid = wxID_SAVE; break;
        case DO_SCREENSHOT: cmdid = ID::Screenshot; break;
        case DO_PREFS:      cmdid = wxID_PREFERENCES; break;
        case DO_QUIT:       cmdid = wxID_EXIT; break;
        
        // Edit menu
        case DO_CUT:        cmdid = wxID_CUT; break;
        case DO_COPY:       cmdid = wxID_COPY; break;
        case DO_PASTE:      cmdid = wxID_PASTE; break;
        case DO_CLEAR:      cmdid = wxID_CLEAR; break;
        case DO_SELALL:     cmdid = wxID_SELECTALL; break;
        
        // View menu
        case DO_FULLSCREEN: cmdid = ID::FullScreen; break;
        case DO_FIT:        cmdid = ID::FitPattern; break;
        case DO_WIREFRAME:  cmdid = ID::Wireframe; break;
        case DO_PATTERNS:   cmdid = ID::PatternsPane; break;
        case DO_RULE:       cmdid = ID::RulePane; break;
        case DO_HELP:       cmdid = ID::HelpPane; break;
        case DO_RESTORE:    cmdid = ID::RestoreDefaultPerspective; break;
        case DO_CHEMICAL:   cmdid = ID::ChangeActiveChemical; break;
        
        // Action menu
        case DO_STEP:       cmdid = ID::Step; break;
        case DO_RUNSTOP:    cmdid = ID::RunStop; break;
        case DO_RESET:      cmdid = ID::Reset; break;
        case DO_GENPATT:    cmdid = ID::GenerateInitialPattern; break;
        case DO_DEVICE:     cmdid = ID::SelectOpenCLDevice; break;
        case DO_OPENCL:     cmdid = ID::OpenCLDiagnostics; break;
        
        // Help menu
        case DO_ABOUT:      cmdid = wxID_ABOUT; break;
        
        default:            Warning(_("Bug detected in ProcessKey!"));
    }
   
    if (cmdid != 0) {
        wxCommandEvent cmdevent(wxEVT_COMMAND_MENU_SELECTED, cmdid);
        cmdevent.SetEventObject(this);
        this->GetEventHandler()->ProcessEvent(cmdevent);
    }
}

void MyFrame::OnChar(wxKeyEvent& event)
{
    // this handler is connected to the render window (pVTKWindow)
    int key = event.GetKeyCode();
    int mods = event.GetModifiers();
    
    ProcessKey(key, mods);
    
    // don't call default handler (wxVTKRenderWindowInteractor::OnChar)
    // event.Skip();
}
